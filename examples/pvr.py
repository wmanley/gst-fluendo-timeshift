# -*- Mode: Python -*-
# vi:si:et:sw=4:sts=4:ts=4
#
# GStreamer Time Shifting
# Copyright (C) 2011 Fluendo S.A. <support@fluendo.com>
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Library General Public
# License as published by the Free Software Foundation; either
# version 2 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Library General Public License for more details.
#
# You should have received a copy of the GNU Library General Public
# License along with this library; if not, write to the
# Free Software Foundation, Inc., 59 Temple Place - Suite 330,
# Boston, MA 02111-1307, USA.
#

import pygtk
pygtk.require('2.0')

import sys

import gobject
gobject.threads_init()

import pygst
pygst.require('0.10')
import gst
import gst.interfaces
import gtk
gtk.gdk.threads_init()

import threading
import time
import os
import string
import shutil

class DvbScanner(threading.Thread):
    def __init__(self, adapter=0, frontend=0, scanning_complete_cb=None,
        frequency_scanned_cb=None, channel_added_cb=None):
        super(DvbScanner, self).__init__()
        self.adapter = adapter
        self.frontend = frontend
        self.adaptertype = None
        self.frequencies_to_scan = []
        self.channels = {} # service id -> transport stream id
        self.current_tuning_params = {}
        self.frequencies_scanned = []
        self.transport_streams = {} # ts id -> tuning_params
        self.pipeline = None
        self.locked = False
        self.nit_arrived = False
        self.sdt_arrived = False
        self.pat_arrived = False
        self.check_for_lock_event_id = None
        self.wait_for_tables_event_id = None
        self.scanning_complete_cb = scanning_complete_cb
        self.frequency_scanned_cb = frequency_scanned_cb
        self.channel_added_cb = channel_added_cb

    def run(self):
        self.pipeline = gst.parse_launch(
            "dvbsrc name=dvbsrc adapter=%d frontend=%d pids=0:16:17:18 " \
            "stats-reporting-interval=0 ! mpegtsparse ! " \
            "fakesink silent=true" % (self.adapter, self.frontend))
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
        bus.connect("message", self.bus_watch_func)
        self.pipeline.set_state(gst.STATE_READY)
        self.pipeline.get_state()
        self.start_scanning()

    def stop(self):
        print "scanner stop"

    def wait_for_tables(self):
        self.pipeline.set_state(gst.STATE_READY)
        self.pipeline.get_state()
        self.locked = False
        self.scanned = False
        if self.nit_arrived and self.sdt_arrived:
            if self.adaptertype == "DVB-T":
                self.frequencies_scanned.append(
                    self.current_tuning_params["frequency"])
            elif self.adaptertype == "DVB-S":
                self.frequencies_scanned.append(
                    (self.current_tuning_params["frequency"],
                     self.current_tuning_params["polarization"]))
            if self.frequency_scanned_cb:
                self.frequency_scanned_cb(
                    self.current_tuning_params["frequency"])
        for f in self.frequencies_to_scan:
            key = f["frequency"]
            if self.adaptertype == "DVB-S":
                key = (f["frequency"], f["polarization"])
            if key in self.frequencies_scanned:
                self.frequencies_to_scan.remove(f)
            else:
                time.sleep(2)
                self.scan(f)
                self.scanned = True
                break
        if not self.scanned:
            if self.scanning_complete_cb:
                self.scanning_complete_cb()
        return False

    def check_for_lock(self):
        if not self.locked:
            self.pipeline.set_state(gst.STATE_READY)
        self.pipeline.get_state()
        self.scanned = False

        for f in self.frequencies_to_scan:
            key = f["frequency"]
            if self.adaptertype == "DVB-S":
                key = (f["frequency"], f["polarization"])
            if key in self.frequencies_scanned:
                self.frequencies_to_scan.remove(f)
            else:
                time.sleep(2)
                self.scan(f)
                self.scanned = True
                break
        if not self.scanned:
            self.scanning_complete_cb()
        return False

    def have_dvb_adapter_type(self, type):
        self.adaptertype = type
        print "Adapter type is %s" % type

    def bus_watch_func(self, bus, message):
        t = message.type
        #print "Bus watch function for message %r" % message
        if t == gst.MESSAGE_ELEMENT:
            if message.structure.get_name() == 'dvb-adapter':
                s = message.structure
                self.have_dvb_adapter_type(s["type"])
            elif message.structure.get_name() == 'dvb-frontend-stats':
                s = message.structure
                if s["lock"] and not self.locked:
                    self.locked = True
                    gobject.source_remove(self.check_for_lock_event_id)
                    self.check_for_lock_event_id = None
                    self.wait_for_tables_event_id = gobject.timeout_add(
                        10*1000,
                        self.wait_for_tables)
            elif message.structure.get_name() == 'dvb-read-failure':
                if self.check_for_lock_event_id:
                    gobject.source_remove(self.check_for_lock_event_id)
                    self.check_for_lock_event_id = None
                if self.wait_for_tables_event_id:
                    gobject.source_remove(self.wait_for_tables_event_id)
                    self.wait_for_tables_event_id = None
                self.wait_for_tables()
            elif message.structure.get_name() == 'sdt':
                s = message.structure
                services = s["services"]
                tsid = s["transport-stream-id"]
                actual = s["actual-transport-stream"]
                if actual:
                    for service in services:
                        name = service.get_name()
                        sid = int(name[8:])
                        if service.has_key("name"):
                            name = service["name"]
                        if self.channels.has_key(sid):
                            self.channels[sid]["name"] = name
                            self.channels[sid]["transport-stream-id"] = tsid
                        else:
                            self.channels[sid] = { "name": name, \
                                "transport-stream-id": tsid }
                        if self.channel_added_cb:
                            self.channel_added_cb(sid, self.channels[sid])
                    self.sdt_arrived = True

            elif message.structure.get_name() == 'nit':
                s = message.structure
                name = s["network-id"]
                actual = s["actual-network"]
                if s.has_key("network-name"):
                    name = s["network-name"]
                transports = s["transports"]
                for transport in transports:
                    tsid = transport["transport-stream-id"]
                    if transport.has_key("delivery"):
                        delivery = transport["delivery"]
                        if delivery["frequency"] not in \
                            self.frequencies_scanned:
                            self.frequencies_to_scan.append(delivery)
                            self.transport_streams[tsid] = delivery

                        if transport.has_key("channels"):
                            chans = transport["channels"]
                            for chan in chans:
                                if self.channels.has_key(
                                    chan["service-id"]):
                                    self.channels[chan["service-id"]]["logical-channel-number"] = chan["logical-channel-number"]
                                else:
                                    self.channels[chan["service-id"]] = { "logical-channel-number" : chan["logical-channel-number"] }
                self.nit_arrived = True
            elif message.structure.get_name() == 'pat':
                programs = message.structure["programs"]
                for p in programs:
                    sid = p["program-number"]
                    pmt = p["pid"]
                    if self.channels.has_key(sid):
                        self.channels[sid]["pmt-pid"] = pmt
                    else:
                        self.channels[sid] = { "pmt-pid":pmt }
                self.pat_arrived = True

        if self.sdt_arrived and self.nit_arrived and self.pat_arrived:
            gobject.source_remove(self.wait_for_tables_event_id)
            self.wait_for_tables()

    def scan(self,tuning_params):
        self.frequencies_to_scan.remove(tuning_params)
        self.current_tuning_params = tuning_params
        self.sdt_arrived = False
        self.nit_arrived = False
        self.pat_arrived = False

        if self.adaptertype == "DVB-T":
            modulation=""
            if tuning_params["constellation"].startswith("QAM"):
                modulation="QAM %s" % tuning_params["constellation"][3:]
            elif tuning_params["constellation"] == "reserved":
                modulation = "QAM 64"
            else:
                modulation = tuning_params["constellation"]
            for param in ["code-rate-hp", "code-rate-lp"]:
                if tuning_params[param] == "reserved":
                    tuning_params[param] = "NONE"
            if tuning_params["hierarchy"] ==  0:
                tuning_params["hierarchy"] = "NONE"
            dvbsrc = self.pipeline.get_by_name("dvbsrc")
            dvbsrc.set_property("frequency", tuning_params["frequency"])
            dvbsrc.set_property("bandwidth", str(tuning_params["bandwidth"]))
            dvbsrc.set_property("code-rate-hp",
                str(tuning_params["code-rate-hp"]))
            dvbsrc.set_property("code-rate-lp",
                str(tuning_params["code-rate-lp"]))
            dvbsrc.set_property("trans-mode",
                str(tuning_params["transmission-mode"]))
            dvbsrc.set_property("guard", str(tuning_params["guard-interval"]))
            dvbsrc.set_property("hierarchy", str(tuning_params["hierarchy"]))
            dvbsrc.set_property("modulation", modulation)
        elif self.adaptertype == "DVB-S":
            if tuning_params["inner-fec"] == "reserved" or \
               tuning_params["inner-fec"] == "none":
                tuning_params["inner-fec"] = "NONE"
            dvbsrc = self.pipeline.get_by_name("dvbsrc")
            dvbsrc.set_property("frequency", tuning_params["frequency"])
            dvbsrc.set_property("polarity", tuning_params["polarization"][0])
            dvbsrc.set_property("symbol-rate", tuning_params["symbol-rate"])
            dvbsrc.set_property("code-rate-hp", tuning_params["inner-fec"])
        elif self.adaptertype == "DVB-C":
            if tuning_params["inner-fec"] == "reserved" or \
               tuning_params["inner-fec"] == "none":
                tuning_params["inner-fec"] = "NONE"

            dvbsrc = self.pipeline.get_by_name("dvbsrc")
            dvbsrc.set_property("inversion", "AUTO")
            dvbsrc.set_property("frequency", tuning_params["frequency"])
            dvbsrc.set_property("symbol-rate", tuning_params["symbol-rate"])
            dvbsrc.set_property("code-rate-hp", tuning_params["inner-fec"])
            modulation=""
            if tuning_params["modulation"].startswith("QAM"):
                modulation="QAM %s" % tuning_params["modulation"][3:]
            elif tuning_params["modulation"] == "reserved":
                modulation = "QAM 64"
            else:
                modulation = tuning_params["modulation"]
            dvbsrc.set_property("modulation", modulation)

        self.pipeline.set_state(gst.STATE_PLAYING)
        (statereturn, state, pending) = self.pipeline.get_state()
        if statereturn == gst.STATE_CHANGE_FAILURE:
            locked = False
            self.check_for_lock()
        else:
            self.locked = False
            # wait 10 seconds for lock
            self.check_for_lock_event_id = gobject.timeout_add(10*1000,
                self.check_for_lock)

    def add_brute_force_scan(self):
        for chan in range(5, 13):
            freq = 142500000 + chan * 7000000
            for transmode in ["2k", "8k"]:
                for guard in ["AUTO", "32", "16", "8", "4"]:
                    self.frequencies_to_scan.append(
                        { "frequency":freq, "bandwidth":7,
                            "code-rate-hp":"NONE", "code-rate-lp":"AUTO",
                            "constellation":"QAM64",
                            "transmission-mode":transmode,
                            "guard-interval":guard,
                            "hierarchy":"AUTO"})

        for chan in range(21, 70):
            freq = 306000000 + chan* 8000000
            for transmode in ["2k", "8k"]:
                for guard in ["32", "16", "8", "4"]:
                    self.frequencies_to_scan.append(
                        { "frequency":freq, "bandwidth":8,
                            "code-rate-hp":"NONE", "code-rate-lp":"AUTO",
                            "constellation":"QAM64",
                            "transmission-mode":transmode,
                            "guard-interval":guard,
                            "hierarchy":"AUTO"})

    def add_tuning_data(self, tuning_data):
        self.frequencies_to_scan.append(tuning_data)

    def start_scanning(self):
        f = self.frequencies_to_scan[0]
        gobject.idle_add(self.scan, f)

    def output_results(self):
        filename = os.getenv("GST_DVB_CHANNELS_CONF")
        if not filename:
            gstversion = gst.version()
            filename = os.path.expanduser(
                "~/.gstreamer-%d.%d/dvb-channels.conf" % (gstversion[0],
                    gstversion[1]))
        print "Filename: %s" % filename
        file = open(filename, "w")
        lines = []
        for sid in self.channels.keys():
            chan = self.channels[sid]
            tuningparams = None
            line = ""
            #print "%s:::::::::" % chan["name"]
            if chan.has_key("name") and chan["name"]:
                line = "%s:" % chan["name"]
            else:
                line = "[%d]:" % sid
            if chan.has_key("transport-stream-id"):
                tuningparams = \
                    self.transport_streams[chan["transport-stream-id"]]
            else:
                continue
            if self.adaptertype == "DVB-T":
                line = "%s%d:INVERSION_AUTO:" % (line,
                    tuningparams["frequency"])
                line = "%sBANDWIDTH_%s_MHZ:" % (line,
                    tuningparams["bandwidth"])
                fecstr = ""
                if tuningparams["code-rate-hp"] == "1/2":
                    fecstr = "FEC_1_2"
                elif tuningparams["code-rate-hp"] == "2/3":
                    fecstr = "FEC_2_3"
                elif tuningparams["code-rate-hp"] == "3/4":
                    fecstr = "FEC_3_4"
                elif tuningparams["code-rate-hp"] == "5/6":
                    fecstr = "FEC_5_6"
                elif tuningparams["code-rate-hp"] == "7/8":
                    fecstr = "FEC_7_8"
                else:
                    fecstr = "FEC_AUTO"
                line = "%s%s:" % (line, fecstr)
                if tuningparams["code-rate-lp"] == "1/2":
                    fecstr = "FEC_1_2"
                elif tuningparams["code-rate-lp"] == "2/3":
                    fecstr = "FEC_2_3"
                elif tuningparams["code-rate-lp"] == "3/4":
                    fecstr = "FEC_3_4"
                elif tuningparams["code-rate-lp"] == "5/6":
                    fecstr = "FEC_5_6"
                elif tuningparams["code-rate-lp"] == "7/8":
                    fecstr = "FEC_7_8"
                else:
                    fecstr = "FEC_AUTO"
                line = "%s%s:" % (line, fecstr)
                constellation = ""
                if tuningparams["constellation"] == "QPSK":
                    constellation = "QPSK"
                elif tuningparams["constellation"] == "QAM16":
                    constellation = "QAM_16"
                else:
                    constellation = "QAM_64"
                line = "%s%s:" % (line, constellation)
                if tuningparams["transmission-mode"] == "2k":
                    line = "%sTRANSMISSION_MODE_2K:" % line
                else:
                    line = "%sTRANSMISSION_MODE_8K:" % line
                line = "%sGUARD_INTEVAL_1_%d:" % (line,
                    tuningparams["guard-interval"])
                if tuningparams["hierarchy"] == 0:
                    line = "%sHIERARCHY_NONE:" % line
                else:
                    line = "%sHIERARCHY_%d:" % (line, tuningparams["hierarchy"])
                line = "%s0:0:%d\n" % (line, sid)
            elif self.adaptertype == "DVB-S":
                line = "%s%d:%s:0:%d:0:0:%d\n" % (line,
                    tuningparams["frequency"]/1000,
                    tuningparams["polarization"][0],
                    tuningparams["symbol-rate"], sid)
            elif self.adaptertype == "DVB-C":
                fecstr = ""
                if tuningparams["inner-fec"] == "1/2":
                    fecstr = "FEC_1_2"
                elif tuningparams["inner-fec"] == "2/3":
                    fecstr = "FEC_2_3"
                elif tuningparams["inner-fec"] == "3/4":
                    fecstr = "FEC_3_4"
                elif tuningparams["inner-fec"] == "5/6":
                    fecstr = "FEC_5_6"
                elif tuningparams["inner-fec"] == "7/8":
                    fecstr = "FEC_7_8"
                else:
                    fecstr = "FEC_AUTO"
                modulation = ""
                if tuningparams["modulation"] == "QAM16":
                    modulation = "QAM_16"
                elif tuningparams["modulation"] == "QAM32":
                    modulation = "QAM_32"
                elif tuningparams["modulation"] == "QAM64":
                    modulation = "QAM_64"
                elif tuningparams["modulation"] == "QAM128":
                    modulation = "QAM_128"
                elif tuningparams["modulation"] == "QAM256":
                    modulation = "QAM_256"
                else:
                    modulation = "QAM_64"
                line = "%s%d:INVERSION_AUTO:%d:%s:%s:0:0:%d\n" % (line,
                    tuningparams["frequency"],
                    tuningparams["symbol-rate"],
                    fecstr, modulation, sid)
            lines.append(line)
            print line
        file.writelines(lines)
        file.close()

class GstPlayer:
    DEMUX_BIN = \
        'flumpegshifter name=timeshift recording-template=/tmp/ts.XXXXXX ! ' \
        'flutsdemux name=demux'
    VIDEO_BIN = 'queue name=vqueue ! decodebin2 ! ' \
        'queue name=vdq max-size-buffers=3 max-size-time=0 max-size-bytes=0 ! '\
        'videoscale ! colorspace ! queue name=vcq ! autovideosink name=vsink'
    AUDIO_BIN = 'queue name=aqueue ! decodebin2 ! queue name=adq ! audioconvert ! ' \
        'queue name=acq ! autoaudiosink'

    def __init__(self, scannerUI):
        self.playing = False
        self.pipeline = gst.Pipeline("player")
        self.bin = None
        self.scannerUI = scannerUI
        self.on_eos = False
        self.uri = None;
        self.pcr_configured = False
        self.program = -1
        self.eit_version = -1
        self.has_video = False
        self.has_audio = False

        bus = self.pipeline.get_bus()
        bus.enable_sync_message_emission()
        bus.add_signal_watch()
        bus.connect('sync-message::element', self.on_sync_message)
        bus.connect('message', self.on_message)

    def on_new_pad(self, element, pad):
        padname = pad.get_name()
        caps = pad.get_caps()
        name = caps[0].get_name()
        bin = None
        queue = None

        if 'video' in name and not self.has_video:
            bin = gst.parse_bin_from_description(self.VIDEO_BIN, False)
            if bin:
                queue = bin.get_by_name("vqueue")
                self.has_video = True

        elif 'audio' in name and not self.has_audio:
            bin = gst.parse_bin_from_description(self.AUDIO_BIN, False)
            if bin:
                queue = bin.get_by_name("aqueue")
                self.has_audio = True

        if bin and queue:
            targetpad = queue.get_pad('sink')
            ghostpad = gst.GhostPad('sink', targetpad)
            bin.add_pad(ghostpad)
            self.bin.add(bin)
            bin.set_state(gst.STATE_READY)
            pad.link(ghostpad)
            bin.sync_state_with_parent()
            # ensure to preroll the sinks
            self.bin.lost_state_full(True)

    def reset_pipeline (self):
        if self.bin:
            self.pipeline.set_state(gst.STATE_NULL)
            self.pipeline.remove(self.bin)
        self.has_video = False
        self.has_audio = False
        self.bin = None
        self.pcr_configured = False
        self.program = -1
        self.eit_version = -1
        self.scannerUI.eittextbuffer.set_text("")

    def setup_uri (self):
        self.reset_pipeline()

        # insert an identity with sleep-time to simulate a slower src when
        # reading from a file
        if 'file://' in self.uri:
            bin = gst.parse_bin_from_description('%s ! ' \
                'identity sleep-time=200 ! mpegtsparse ! %s' % \
                (self.uri, self.DEMUX_BIN), False)
        else:
            bin = gst.parse_bin_from_description('%s ! mpegtsparse ! %s' % \
                (self.uri, self.DEMUX_BIN), False)

        if bin:
            self.bin = bin
            demux = bin.get_by_name("demux")
            demux.connect('pad-added', self.on_new_pad)
            self.pipeline.add(bin)
            bin.sync_state_with_parent()

    def setup_dvb(self, modulation, bw, tm, freq, crlp, crhp, g, h, program):
        if self.uri:
             self.setup_uri()
             return

        self.reset_pipeline()
        self.program = program

        bin = gst.parse_bin_from_description('dvbsrc name=dvbsrc ' \
            'modulation="%s" trans-mode=%s bandwidth=%d frequency=%d' \
            ' code-rate-lp=%s code-rate-hp=%s guard=%d hierarchy=%d ! '\
            'mpegtsparse program-numbers="%d" .program_%d ! %s' % \
            (modulation, bw, tm, freq, crlp, crhp, g, h, program, program, \
            self.DEMUX_BIN), False)

        if bin:
            self.bin = bin
            demux = bin.get_by_name("demux")
            demux.connect('pad-added', self.on_new_pad)
            self.pipeline.add(bin)
            bin.sync_state_with_parent()

    def on_recording_started(self, filename):
        self.scannerUI.spinner.start()
        self.scannerUI.recordbutton.set_sensitive(False)

    def on_recording_stopped(self, filename):
        label = gtk.Label("A recording was done, do you want save it?")
        dialog = gtk.Dialog("Recording stopped",
                           None,
                           gtk.DIALOG_MODAL | gtk.DIALOG_DESTROY_WITH_PARENT,
                           (gtk.STOCK_CANCEL, gtk.RESPONSE_REJECT,
                            gtk.STOCK_OK, gtk.RESPONSE_ACCEPT))
        dialog.vbox.pack_start(label)
        label.show()
        response = dialog.run()
        dialog.destroy()
        if response == gtk.RESPONSE_ACCEPT:
            shutil.copyfile(filename, "%s/Videos/recording.ts" % os.environ['HOME'])

    def on_sync_message(self, bus, message):
        if message.structure is None:
            return
        if message.structure.get_name() == 'prepare-xwindow-id':
            # Sync with the X server before giving the X-id to the sink
            gtk.gdk.threads_enter()
            gtk.gdk.display_get_default().sync()
            self.scannerUI.videowidget.set_sink(message.src)
            message.src.set_property('force-aspect-ratio', True)
            gtk.gdk.threads_leave()
        elif message.structure.get_name() == 'pmt' and not self.pcr_configured:
            s = message.structure

            if self.uri and self.program == -1:
                self.program = s['program-number']

            if s['program-number'] == self.program:
                if s.has_key('pcr-pid'):
                    timeshift = self.pipeline.get_by_name("timeshift")
                    timeshift.set_property("pcr-pid", s['pcr-pid'])
                    self.pcr_configured = True
                    print "PCR pid configured %04x" % s['pcr-pid']

        elif message.structure.get_name() == 'eit':
            s = message.structure
            if s['service-id'] == self.program and s['present-following']:
                if self.eit_version != s['version-number']:
                    event = s['events'][0]
                    if event:
                        eit = "%s\n\n%s" % (event['name'], event['description'])
                        self.scannerUI.eittextbuffer.set_text(eit)
                        self.eit_version = s['version-number']

        elif message.structure.get_name() == 'shifter-recording-started':
            s = message.structure
            self.on_recording_started(s['filename'])
        elif message.structure.get_name() == 'shifter-recording-stopped':
            s = message.structure
            self.on_recording_stopped(s['filename'])

    def on_message(self, bus, message):
        t = message.type
        if t == gst.MESSAGE_ERROR:
            err, debug = message.parse_error()
            print "Error: %s" % err, debug
            if self.on_eos:
                self.on_eos()
            self.playing = False
        elif t == gst.MESSAGE_EOS:
            if self.on_eos:
                self.on_eos()
            self.playing = False

    def query_position(self):
        "Returns a (position, duration) tuple"
        try:
            position, format = self.pipeline.query_position(gst.FORMAT_TIME)
        except:
            position = gst.CLOCK_TIME_NONE

        try:
            duration, format = self.pipeline.query_duration(gst.FORMAT_TIME)
        except:
            duration = gst.CLOCK_TIME_NONE

        return (position, duration)

    def seek(self, location):
        """
        @param location: time to seek to, in nanoseconds
        """
        gst.debug("seeking to %r" % location)
        event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
            gst.SEEK_FLAG_FLUSH | gst.SEEK_FLAG_ACCURATE,
            gst.SEEK_TYPE_SET, location,
            gst.SEEK_TYPE_NONE, 0)

        res = self.pipeline.send_event(event)
        if res:
            gst.info("setting new stream time to 0")
            # self.pipeline.set_start_time(0L)
        else:
            gst.error("seek to %r failed" % location)

    def seek_end(self):
        event = gst.event_new_seek(1.0, gst.FORMAT_TIME,
            gst.SEEK_FLAG_FLUSH | gst.SEEK_FLAG_ACCURATE,
            gst.SEEK_TYPE_END, -1,
            gst.SEEK_TYPE_NONE, 0)

        res = self.pipeline.send_event(event)
        if res:
            gst.info("setting new stream time to 0")
        else:
            gst.error("seek to end failed")

    def start_recording(self):
        structure = gst.Structure ("shifter-start-recording")
        event = gst.event_new_custom (gst.EVENT_CUSTOM_UPSTREAM, structure)
        res = self.pipeline.send_event(event)
        if res:
            print "start recording"
        else:
            gst.error("start recording failed")

    def pause(self):
        self.pipeline.set_state(gst.STATE_PAUSED)
        self.playing = False

    def play(self):
        self.pipeline.set_state(gst.STATE_PLAYING)
        self.playing = True
        if self.bin:
            src = self.bin.get_by_name("dvbsrc")
            if src:
                src.set_locked_state(True)

    def stop(self):
        if self.bin:
            src = self.bin.get_by_name("dvbsrc")
            if src:
                src.set_locked_state(False)

        self.pipeline.set_state(gst.STATE_NULL)
        self.playing = False

    def get_state(self, timeout=1):
        return self.pipeline.get_state(timeout=timeout)

    def is_playing(self):
        return self.playing

    def set_uri(self, uri):
        self.uri = uri
        self.setup_uri()

class VideoWidget(gtk.DrawingArea):
    def __init__(self):
        gtk.DrawingArea.__init__(self)
        self.imagesink = None
        self.unset_flags(gtk.DOUBLE_BUFFERED)
        self.size(512, 384)

    def do_expose_event(self, event):
        if self.imagesink:
            self.imagesink.expose()
            return False
        else:
            return True

    def set_sink(self, sink):
        assert self.window.xid
        self.imagesink = sink
        self.imagesink.set_xwindow_id(self.window.xid)

class ScannerUI(gtk.Window):
    UPDATE_INTERVAL = 500

    def __init__(self):
        gtk.Window.__init__(self)
        self.set_default_size(800, 480)

        self.scanner = None
        self.adapter = 1
        self.channels = {}
        self.adapters = []

        self.update_id = -1
        self.changed_id = -1
        self.seek_timeout_id = -1
        self.was_playing = False

        self.p_position = gst.CLOCK_TIME_NONE
        self.p_duration = gst.CLOCK_TIME_NONE

        self.create_ui()

        self.player = GstPlayer(self)

        self.load_default_channel_list()

        def on_delete_event():
            if self.scanner:
                self.scanner.stop()
            if self.player:
                self.player.stop()
            gtk.main_quit()
        self.connect('delete-event', lambda *x: on_delete_event())

    def create_ui(self):
        mainvbox = gtk.VBox()

        hbox = gtk.HBox()
        self.adapterlist = gtk.combo_box_new_text()
        self.adapterlist.connect("changed", self.adapter_changed)
        self.initialtuning = gtk.combo_box_new_text()
        self.find_adapters_and_fill_list()
        hbox.add(self.adapterlist)
        hbox.add(self.initialtuning)
        scanbutton = gtk.Button("Scan")
        scanbutton.connect("clicked", self.scan_button_clicked)
        hbox.add(scanbutton)
        mainvbox.pack_start(hbox, fill=False, expand=False)

        scanvbox = gtk.VBox()
        self.label = gtk.Label("")
        self.progressbar = gtk.ProgressBar()
        scanvbox.pack_start(self.label, True, True, 3)
        scanvbox.pack_start(self.progressbar, True, True, 3)
        mainvbox.pack_start(scanvbox, fill=False, expand=False)

        self.channelstore = gtk.ListStore(str, int, int, int)
        self.channelstore.set_sort_column_id(2, gtk.SORT_ASCENDING)
        self.channelview = gtk.TreeView(self.channelstore)
        self.channelview.set_headers_visible(True)
        self.channelview.set_sensitive(True)
        namecol = gtk.TreeViewColumn("Channel", gtk.CellRendererText(), text=0)
        self.channelview.append_column(namecol)
        self.channelview.connect("row-activated", self.play_channel)
        channelscroll = gtk.ScrolledWindow()
        channelscroll.add(self.channelview)

        label = gtk.Label("Current Program:")

        eitscroll = gtk.ScrolledWindow()
        textview = gtk.TextView()
        textview.set_wrap_mode(gtk.WRAP_WORD)
        self.eittextbuffer = textview.get_buffer()
        eitscroll.add(textview)
        eitscroll.show()
        textview.show()

        channelvbox = gtk.VBox()
        channelvbox.pack_start(channelscroll)
        channelvbox.pack_start(label, False, False, 3)
        channelvbox.pack_start(eitscroll)

        playervbox = gtk.VBox()

        self.videowidget = VideoWidget()
        playervbox.pack_start(self.videowidget)

        hbox = gtk.HBox()
        playervbox.pack_start(hbox, fill=False, expand=False)

        self.pause_image = gtk.image_new_from_stock(gtk.STOCK_MEDIA_PAUSE,
                                                    gtk.ICON_SIZE_BUTTON)
        self.pause_image.show()
        self.play_image = gtk.image_new_from_stock(gtk.STOCK_MEDIA_PLAY,
                                                   gtk.ICON_SIZE_BUTTON)
        self.play_image.show()
        self.playbutton = button = gtk.Button()
        button.add(self.play_image)
        button.set_property('can-default', True)
        button.set_focus_on_click(False)
        button.set_size_request(40, 40)
        button.show()
        hbox.pack_start(button, False, False, 3)
        button.set_property('has-default', False)
        button.connect('clicked', lambda *args: self.play_toggled())

        image = gtk.image_new_from_stock(gtk.STOCK_MEDIA_NEXT,
                                                   gtk.ICON_SIZE_BUTTON)
        image.show()
        button = gtk.Button()
        button.add(image)
        button.set_property('can-default', True)
        button.set_focus_on_click(False)
        button.set_size_request(40, 40)
        button.show()
        hbox.pack_start(button, False, False, 3)
        button.set_property('has-default', False)
        button.connect('clicked', lambda *args: self.seek_end_toggled())

        self.adjustment = gtk.Adjustment(0.0, 0.00, 100.0, 0.1, 1.0, 1.0)
        hscale = gtk.HScale(self.adjustment)
        hscale.set_digits(2)
        hscale.set_update_policy(gtk.UPDATE_CONTINUOUS)
        hscale.connect('button-press-event', self.scale_button_press_cb)
        hscale.connect('button-release-event', self.scale_button_release_cb)
        hscale.connect('format-value', self.scale_format_value_cb)
        hbox.pack_start(hscale, True, True, 3)
        self.hscale = hscale

        image = gtk.image_new_from_stock(gtk.STOCK_MEDIA_RECORD,
                                                   gtk.ICON_SIZE_BUTTON)
        image.show()
        self.recordbutton = button = gtk.ToggleButton()
        button.add(image)
        button.set_focus_on_click(False)
        button.set_size_request(40, 40)
        button.show()
        hbox.pack_start(button, False, False, 3)
        button.connect('clicked', lambda *args: self.recording_toggled())

        self.spinner = spinner = gtk.Spinner()
        spinner.show()
        spinner.size(32, 32)
        hbox.pack_start(spinner, False, True, 3)

        hbox = gtk.HBox()
        hbox.add(channelvbox)
        hbox.add(playervbox)

        mainvbox.add(hbox)

        # add widgets to window
        self.add(mainvbox)

    def play_channel(self, treeview, iter, path):
        self.player.stop()
        self.spinner.stop()
        self.recordbutton.set_active(False)
        self.recordbutton.set_sensitive(True)
        row = self.channelstore[iter]
        self.player.setup_dvb("QAM 64", "8k", 8, row[3], "AUTO", "2/3", 4, 0, row[1])
        self.play_toggled()

    def play_toggled(self):
        self.playbutton.remove(self.playbutton.child)
        if self.player.is_playing():
            self.player.pause()
            self.playbutton.add(self.play_image)
        else:
            self.player.play()
            if self.update_id == -1:
                self.update_id = gobject.timeout_add(self.UPDATE_INTERVAL,
                                                     self.update_scale_cb)
            self.playbutton.add(self.pause_image)

    def recording_toggled(self):
        if self.recordbutton.get_active():
            self.spinner.start()
            self.player.start_recording()
            self.recordbutton.set_sensitive(False)

    def seek_end_toggled(self):
        self.player.seek_end()

    def scale_format_value_cb(self, scale, value):
        if self.p_duration == -1:
            real = 0
        else:
            real = value * self.p_duration / 100

        seconds = real / gst.SECOND

        return "%02d:%02d" % (seconds / 60, seconds % 60)

    def scale_button_press_cb(self, widget, event):
        # see seek.c:start_seek
        gst.debug('starting seek')

        self.playbutton.set_sensitive(False)

        if self.player.is_playing():
            self.player.pause()
            self.was_playing = True

        # don't timeout-update position during seek
        if self.update_id != -1:
            gobject.source_remove(self.update_id)
            self.update_id = -1

        # make sure we get changed notifies
        if self.changed_id == -1:
            self.changed_id = self.hscale.connect('value-changed',
                self.scale_value_changed_cb)

    def scale_value_changed_cb(self, scale):
        real = long(scale.get_value() * self.p_duration / 100) # in ns
        gst.debug('value changed, perform seek to %r' % real)
        self.player.seek(real)
        # allow for a preroll
        self.player.get_state(timeout=50*gst.MSECOND) # 50 ms

    def scale_button_release_cb(self, widget, event):
        widget.disconnect(self.changed_id)
        self.changed_id = -1

        self.playbutton.set_sensitive(True)
        if self.seek_timeout_id != -1:
            gobject.source_remove(self.seek_timeout_id)
            self.seek_timeout_id = -1
        else:
            gst.debug('released slider, setting back to playing')
            if self.was_playing:
                self.player.play()

        if self.update_id != -1:
            self.error('Had a previous update timeout id')
        else:
            self.update_id = gobject.timeout_add(self.UPDATE_INTERVAL,
                self.update_scale_cb)

    def update_scale_cb(self):
        self.p_position, self.p_duration = self.player.query_position()
        if self.p_position != gst.CLOCK_TIME_NONE:
            value = self.p_position * 100.0 / self.p_duration
            self.adjustment.set_value(value)

        return True

    def adapter_changed(self, combobox):
         adapter, = self.adapterlist.get_model().get(
            self.adapterlist.get_active_iter(), 0)
         self.adapter = int(adapter[len(adapter)-1])
         self.fill_list_of_available_initial_tuning_info(self.adapters[self.adapter])

    def find_adapters_and_fill_list(self):
        for i in range (0,8):
            if os.path.exists('/dev/dvb/adapter%d/frontend0' % i):
                adaptertype = self.get_type_of_adapter(i)
                self.adapterlist.append_text("DVB (%s) Adapter %d" % (
                    adaptertype,i))
                self.adapters.append(adaptertype)
        if len(self.adapters) > 0:
            self.adapterlist.set_active(0)

    def load_default_channel_list (self):
        filename = os.getenv("GST_DVB_CHANNELS_CONF")
        if not filename:
            gstversion = gst.version()
            filename = os.path.expanduser(
                "~/.gstreamer-%d.%d/dvb-channels.conf" % (gstversion[0],
                    gstversion[1]))
        if os.path.exists(filename):
            f = open(filename, "r")
            while True:
                line = f.readline()
                if not line:
                    break
                if line[0] != '#':
                    params = line.split(":")
                    if len(params) == 13:
                        sid = int(params[12]);
                        c = { "name":params[0] }
                        t = { "frequency":int(params[1]) }
                        self.update_channel (sid, c, t)
            f.close()

    def fill_list_of_available_initial_tuning_info(self, adaptertype):
        # clear the current combobox
        liststore = gtk.ListStore(gobject.TYPE_STRING)
        self.initialtuning.set_model(liststore)
        if adaptertype == "DVB-T":
            self.initialtuning.append_text(
                "Unknown (will do a brute-force scan)")
        tuningfiles = []
        for path in ["/usr/share/dvb", "/usr/share/dvb-apps",
            "/usr/share/doc/dvb-utils/examples/scan"]:
            if os.path.exists(path):
                for f in os.listdir(os.path.join(path,
                    string.lower(adaptertype))):
                    tuningfiles.append(f)

        tuningfiles.sort()
        for f in tuningfiles:
            self.initialtuning.append_text(f)

        if len(liststore) > 0:
            self.initialtuning.set_active(0)

    def get_type_of_adapter(self, adapter):
        dvbelement = gst.element_factory_make("dvbsrc", "test_dvbsrc")
        dvbelement.set_property("adapter", adapter)
        pipeline = gst.Pipeline("")
        pipeline.add(dvbelement)
        pipeline.set_state(gst.STATE_READY)
        pipeline.get_state()
        bus = pipeline.get_bus()
        adaptertype = None
        while bus.have_pending():
            msg = bus.pop()
            if msg.type == gst.MESSAGE_ELEMENT and msg.src == dvbelement:
                structure = msg.structure
                if structure.get_name() == "dvb-adapter":
                    adaptertype = structure["type"]
                    break
        pipeline.set_state(gst.STATE_NULL)
        return adaptertype

    def scan_button_clicked(self, button):
        self.player.stop()
        adapter, = self.adapterlist.get_model().get(
            self.adapterlist.get_active_iter(), 0)
        self.adapter = int(adapter[len(adapter)-1])
        self.channelstore.clear()
        self.start_scan()

    def update_channel(self, sid, chan, tuning=None):
        # check if we already have this channel
        channum =  sid
        channame = ""
        if not chan.has_key("name"):
            pass
        else:
            channame = chan["name"]
        if chan.has_key("logical-channel-number"):
            channum = chan["logical-channel-number"]

        if self.channels.has_key("channame"):
            pass
        else:
            self.channels[channame] = "dvb://%d" % channum
            #self.label.set_text("Scanning: %d channels found" %
            #    len(self.channels))
        iter = self.channelstore.get_iter_root()
        found = False
        channum =  sid
        if chan.has_key("logical-channel-number"):
            channum = chan["logical-channel-number"]
        while iter:
            if self.channelstore.get_value(iter, 1) == sid:
                found = True
                break
            iter = self.channelstore.iter_next(iter)
        if not found:
            iter = self.channelstore.append((chan["name"], sid, channum, 0))
        else:
            self.channelstore.set_value(iter, 0, chan["name"])
            self.channelstore.set_value(iter, 1, sid)
            self.channelstore.set_value(iter, 2, channum)
        if tuning:
            self.channelstore.set_value(iter, 3, tuning["frequency"])

        # modulation, bw, tm, freq, crlp, crhp, g, h

    def frequency_scanned(self, freq):
        self.label.set_text("Scanning: Frequency %d complete" % freq)
        self.progressbar.pulse()

    def scanning_complete(self):
        self.label.set_text("Scanning complete: %d channels found" %
            len(self.channels))
        self.scanner.output_results()

    def start_scan(self):
        self.scanner = DvbScanner(adapter=self.adapter,frontend=0,
            scanning_complete_cb=self.scanning_complete,
            frequency_scanned_cb = self.frequency_scanned,
            channel_added_cb = self.update_channel)

        initialtuning, = self.initialtuning.get_model().get(
            self.initialtuning.get_active_iter(), 0)
        if initialtuning == "Unknown (will do a brute-force scan)":
            self.scanner.add_brute_force_scan()
        else:
            # need to parse file to get tuning data
            itd = self.get_initial_tuning_data(self.adapters[self.adapter],
                initialtuning)
            for data in itd:
                self.scanner.add_tuning_data(data)
                print data

        self.label.set_text("Scanning:")
        self.scanner.run()

    def get_initial_tuning_data(self, adaptertype, initialtuningfile):
        ret = []
        for path in ["/usr/share/dvb", "/usr/share/dvb-apps",
            "/usr/share/doc/dvb-utils/examples/scan"]:
            filename = os.path.join(path, string.lower(adaptertype),
                    initialtuningfile)
            if os.path.exists(filename):
                f = open(filename, "r")
                while True:
                    line = f.readline()
                    if not line:
                        break
                    if line[0] != '#':
                        params = line[:(line.find('#'))].strip().split(" ")
                        if params[0] == "T" and adaptertype == "DVB-T":
                            if len(params) == 9:
                                d = { "frequency":int(params[1]),
                                    "bandwidth":int(params[2][0]),
                                    "code-rate-hp":params[3],
                                    "code-rate-lp":params[4],
                                    "constellation":params[5],
                                    "transmission-mode":params[6],
                                    "guard-interval":int(params[7].split("/")[1]),
                                    "hierarchy":params[8]}
                                ret.append(d)

                        elif params[0] == "S" and adaptertype == "DVB-S":
                            if len(params) == 5:

                                d = { "frequency":int(params[1]),
                                    "symbol-rate":int(params[3])/1000,
                                    "inner-fec":params[4] }
                                if params[2] == "V":
                                    d["polarization"] = "vertical"
                                else:
                                    d["polarization"] = "horizontal"
                                ret.append(d)
                        elif params[0] == "C" and adaptertype == "DVB-C":
                            if len(params) == 5:
                                d = { "frequency":int(params[1]),
                                    "symbol-rate":int(params[2])/1000,
                                    "inner-fec":params[3],
                                    "modulation":params[4] }
                                ret.append(d)
                f.close()
        return ret

    def set_uri(self, uri):
        self.player.set_uri(uri);

class Scanner:
    def __init__(self):
        self.window = ScannerUI()
        self.window.show_all()

    def set_uri(self, uri):
        self.window.set_uri(uri);

    def frequency_scanned(self, freq):
        pass

    def channel_added(self, sid, chan):
        self.window.update_channel(sid, chan)

    def fill_tunning_info(self):
        channels = self.scanner.channels
        transport_streams = self.scanner.transport_streams
        for sid in channels.keys():
            chan = channels[sid]
            if chan.has_key("transport-stream-id"):
                tuning = transport_streams[chan["transport-stream-id"]]
            else:
                continue
            self.window.update_channel(sid, chan, tuning)

    def scanning_complete(self, blah=None):
        print "Scanning complete"
        self.scanner.output_results()
        self.fill_tunning_info()
        self.window.scanning_complete()

def main(args):

    # Need to register our derived widget types for implicit event
    # handlers to get called.
    gobject.type_register(ScannerUI)
    gobject.type_register(VideoWidget)

    s = Scanner();

    if len(args) == 2:
        if gst.uri_is_valid(args[1]):
            s.set_uri(args[1])
        else:
            sys.stderr.write("Error: Invalid URI: %s\n" % args[1])
            sys.exit(1)

    gtk.main()

if __name__ == '__main__':
    sys.exit(main(sys.argv))

