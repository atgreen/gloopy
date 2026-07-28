;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: AGPL-3.0-only

;;;; osc.lisp — Gloopy's live performance lane over OSC (127.0.0.1:9000, UDP).
;;;;
;;;; The fire-and-forget half of the control API: note on/off, CC, real-time
;;;; knob turns, and fire-and-forget transport.  gRPC (the GLOOPY package)
;;;; *creates and names* things and hands back stable ids; OSC *performs* on
;;;; those ids with the lowest possible latency.  See ../docs/CONTROL-API.md.
;;;;
;;;; Unlike a hand-rolled encoder, this lane rides the third-party `osc'
;;;; library for the wire format: osc:make-message + osc::encode-osc-data tag
;;;; each argument by Lisp type — integers become OSC int32 (`i'), floats
;;;; become float32 (`f') — which is exactly Gloopy's address-space contract.
;;;; (We drive the datagram socket with usocket directly; the osc library's
;;;; own "devices" transmitter layer is skipped, so encoding is all we borrow.)
;;;;
;;;;   /gloopy/track/<id>/note   <pitch:i> <vel:i>    vel>0 on, vel=0 off
;;;;   /gloopy/track/<id>/cc     <cc:i> <val:f>        val 0..1
;;;;   /gloopy/track/<id>/vol    <v:f>                 0..1
;;;;   /gloopy/track/<id>/pan    <p:f>                 -1..1
;;;;   /gloopy/track/<id>/mute   <m:i>                 0/1
;;;;   /gloopy/insert/<n>/fx/<m>/param/<name>  <v:f>
;;;;   /gloopy/transport/{play,stop}                   (no args)
;;;;   /gloopy/transport/tempo   <bpm:f>
;;;;   /gloopy/transport/seek    <beats:f>

(in-package :gloopy.osc)

(defparameter *host* "127.0.0.1" "Host Gloopy's OSC receiver listens on.")
(defparameter *port* 9000        "UDP port Gloopy's OSC receiver listens on.")
(defvar *transmitter* nil
  "The active usocket datagram socket, or NIL if not connected.")

;;; --- lifecycle --------------------------------------------------------------
(defun connectedp ()
  "True if the OSC datagram socket is open."
  (and *transmitter* t))

(defun connect (&key (host *host*) (port *port*))
  "Open (or reopen) the UDP socket to Gloopy's OSC lane.  Fire-and-forget:
UDP has no handshake, so this simply prepares the socket — it does not confirm
anything is listening."
  (disconnect)
  (setf *host* host *port* port
        *transmitter* (usocket:socket-connect host port :protocol :datagram))
  (values))

(defun disconnect ()
  "Close the OSC socket, if open."
  (when *transmitter*
    (ignore-errors (usocket:socket-close *transmitter*))
    (setf *transmitter* nil))
  (values))

(defmacro with-osc ((&key (host '*host*) (port '*port*)) &body body)
  "Open the OSC lane for the extent of BODY, closing it afterwards."
  `(progn
     (connect :host ,host :port ,port)
     (unwind-protect (progn ,@body)
       (disconnect))))

(defun %encode (address args)
  "OSC-encode ADDRESS + ARGS to a (vector (unsigned-byte 8)) via the osc library.
Integers tag as int32 (`i'), reals as float32 (`f') — Gloopy's contract."
  (osc::encode-osc-data (osc:make-message address args)))

(defun %send (address &rest args)
  "Encode an OSC message for ADDRESS with ARGS (via the osc library) and fire it
over the datagram socket, auto-connecting on first use.  Integers are tagged
int32, reals float32 — matching Gloopy's contract."
  (unless (connectedp) (connect))
  (let ((bytes (%encode address args)))
    ;; Fire-and-forget: on a connected UDP socket a *previous* packet hitting a
    ;; closed port comes back as ECONNREFUSED on the *next* send.  Gloopy's live
    ;; lane must not die because one packet met no listener, so we tolerate it.
    (handler-case
        (usocket:socket-send *transmitter* bytes (length bytes))
      (usocket:connection-refused-error () nil)))
  (values))

;; Coercions that pin the OSC type tag: `i' wants an integer, `f' a single-float.
(declaim (inline i f))
(defun i (x) (round x))
(defun f (x) (coerce x 'single-float))

;;; --- live MIDI --------------------------------------------------------------
(defun note-on (track pitch &optional (velocity 100))
  "Sound MIDI PITCH on TRACK at VELOCITY (1..127) right now."
  (%send (format nil "/gloopy/track/~d/note" (i track)) (i pitch) (i velocity)))

(defun note-off (track pitch)
  "Release MIDI PITCH on TRACK (velocity 0)."
  (%send (format nil "/gloopy/track/~d/note" (i track)) (i pitch) 0))

(defun chord (track pitches &optional (velocity 100))
  "Sound every pitch in PITCHES on TRACK at once."
  (dolist (p pitches) (note-on track p velocity))
  (values))

(defun cc (track controller value)
  "Send MIDI CONTROLLER (0..127) on TRACK; VALUE is 0..1 (Gloopy scales to 0..127)."
  (%send (format nil "/gloopy/track/~d/cc" (i track)) (i controller) (f value)))

;;; --- track params (atomic lane) ---------------------------------------------
(defun vol (track v)
  "Set TRACK volume, 0..1."
  (%send (format nil "/gloopy/track/~d/vol" (i track)) (f v)))

(defun pan (track p)
  "Set TRACK pan, -1..1."
  (%send (format nil "/gloopy/track/~d/pan" (i track)) (f p)))

(defun mute (track &optional (on t))
  "Mute (ON true) or unmute TRACK."
  (%send (format nil "/gloopy/track/~d/mute" (i track)) (if on 1 0)))

;;; --- effects (atomic lane) --------------------------------------------------
(defun fx-param (insert slot name value)
  "Turn effect knob NAME (a string) on mixer INSERT, effect SLOT, to VALUE."
  (%send (format nil "/gloopy/insert/~d/fx/~d/param/~a" (i insert) (i slot) name)
         (f value)))

;;; --- transport (fire-and-forget) --------------------------------------------
(defun play  () "Start the transport."          (%send "/gloopy/transport/play"))
(defun stop  () "Stop the transport and rewind." (%send "/gloopy/transport/stop"))
(defun tempo (bpm)   "Set tempo in BPM."   (%send "/gloopy/transport/tempo" (f bpm)))
(defun seek  (beats) "Jump to BEATS."      (%send "/gloopy/transport/seek"  (f beats)))

;;; --- demo -------------------------------------------------------------------
(defun demo (&key (track 5) (velocity 95))
  "Play an A-minor arpeggio on TRACK — a quick did-it-connect check."
  (dolist (n '(57 60 64 69 72 69 64 60))
    (note-on track n velocity) (sleep 0.22)
    (note-off track n)         (sleep 0.04))
  (values))
