;;;; gloopy-osc.lisp — drive Gloopy's OSC control lane from Common Lisp (SBCL)
;;;;
;;;; Gloopy listens for OSC on UDP 127.0.0.1:9000 and prints its track ids on
;;;; startup, e.g.  [osc] tracks: 0=Kick 1=Snare ... 5=Pad 6=Lead ...
;;;;
;;;; Usage:
;;;;   (ql:quickload :usocket)     ; or (require :usocket)
;;;;   (load "gloopy-osc.lisp")
;;;;   (in-package :gloopy)
;;;;   (note-on 5 60 100)          ; middle C on track 5 (the Surge pad)
;;;;   (chord   5 '(57 60 64))     ; an A-minor chord
;;;;   (set-tempo 128.0)
;;;;   (effect-param 6 0 "Wet" 0.6); reverb wet on insert 6, effect slot 0
;;;;   (demo)                      ; a little arpeggio

(require :usocket)

(defpackage :gloopy
  (:use :cl)
  (:export :note-on :note-off :chord :cc :track-vol :track-pan :track-mute
           :play :stop :set-tempo :seek :effect-param :demo))
(in-package :gloopy)

(defparameter *host* "127.0.0.1")
(defparameter *port* 9000)
(defvar *sock* (usocket:socket-connect *host* *port* :protocol :datagram))

;;; --- minimal OSC encoding (no external OSC lib needed) ---
(defun pad4 (bytes)                       ; null-terminate + pad to a multiple of 4
  (let ((v (append bytes '(0))))
    (append v (make-list (mod (- 4 (mod (length v) 4)) 4) :initial-element 0))))

(defun i32 (n)                            ; big-endian int32
  (let ((n (logand n #xffffffff)))
    (list (ldb (byte 8 24) n) (ldb (byte 8 16) n) (ldb (byte 8 8) n) (ldb (byte 8 0) n))))

(defun f32 (x)                            ; big-endian IEEE-754 single (SBCL)
  (i32 (sb-kernel:single-float-bits (coerce x 'single-float))))

(defun osc-send (address &rest args)      ; ints -> 'i', reals -> 'f'
  (let* ((addr (pad4 (map 'list #'char-code address)))
         (tags (with-output-to-string (s)
                 (write-char #\, s)
                 (dolist (a args) (write-char (if (integerp a) #\i #\f) s))))
         (tagb (pad4 (map 'list #'char-code tags)))
         (data (loop for a in args append (if (integerp a) (i32 a) (f32 a))))
         (msg  (coerce (append addr tagb data) '(vector (unsigned-byte 8)))))
    (usocket:socket-send *sock* msg (length msg))))

;;; --- API ---
(defun note-on   (track pitch &optional (vel 100)) (osc-send (format nil "/gloopy/track/~d/note" track) pitch vel))
(defun note-off  (track pitch)                     (osc-send (format nil "/gloopy/track/~d/note" track) pitch 0))
(defun chord     (track pitches &optional (vel 100)) (dolist (p pitches) (note-on track p vel)))
(defun cc        (track num val)  (osc-send (format nil "/gloopy/track/~d/cc" track) num (coerce val 'single-float)))
(defun track-vol (track v)  (osc-send (format nil "/gloopy/track/~d/vol"  track) (coerce v 'single-float)))
(defun track-pan (track p)  (osc-send (format nil "/gloopy/track/~d/pan"  track) (coerce p 'single-float)))
(defun track-mute(track m)  (osc-send (format nil "/gloopy/track/~d/mute" track) (if m 1 0)))
(defun play      ()      (osc-send "/gloopy/transport/play"))
(defun stop      ()      (osc-send "/gloopy/transport/stop"))
(defun set-tempo (bpm)   (osc-send "/gloopy/transport/tempo" (coerce bpm 'single-float)))
(defun seek      (beats) (osc-send "/gloopy/transport/seek"  (coerce beats 'single-float)))
(defun effect-param (insert slot name value)
  (osc-send (format nil "/gloopy/insert/~d/fx/~d/param/~a" insert slot name) (coerce value 'single-float)))

;;; --- demo: an A-minor arpeggio on the Surge pad (track 5) ---
(defun demo ()
  (dolist (n '(57 60 64 69 72 69 64 60))
    (note-on 5 n 95) (sleep 0.22) (note-off 5 n) (sleep 0.04)))
