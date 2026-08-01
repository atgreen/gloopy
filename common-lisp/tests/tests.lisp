;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: AGPL-3.0-only

;;;; tests.lisp — offline tests for the Gloopy Common Lisp client.
;;;;
;;;; These need NO running Gloopy: they exercise the OSC wire encoding (which is
;;;; the substance of the live lane) and the pure gRPC-side helpers.  Run with:
;;;;     (asdf:test-system :gloopy)

(defpackage :gloopy-tests
  (:use :cl :fiveam))
(in-package :gloopy-tests)

(def-suite gloopy :description "Offline tests for the Gloopy client.")
(in-suite gloopy)

;;; --- an independent reference OSC encoder -----------------------------------
;;; Big-endian, null-terminated + 4-byte padded; ints -> 'i', reals -> 'f'.
(defun pad4 (bytes)
  (let ((v (append bytes '(0))))
    (append v (make-list (mod (- 4 (mod (length v) 4)) 4) :initial-element 0))))
(defun i32 (n)
  (let ((n (logand n #xffffffff)))
    (list (ldb (byte 8 24) n) (ldb (byte 8 16) n) (ldb (byte 8 8) n) (ldb (byte 8 0) n))))
(defun f32 (x) (i32 (sb-kernel:single-float-bits (coerce x 'single-float))))
(defun ref-osc (address &rest args)
  (let* ((addr (pad4 (map 'list #'char-code address)))
         (tags (with-output-to-string (s)
                 (write-char #\, s)
                 (dolist (a args) (write-char (if (integerp a) #\i #\f) s))))
         (tagb (pad4 (map 'list #'char-code tags)))
         (data (loop for a in args append (if (integerp a) (i32 a) (f32 a)))))
    (append addr tagb data)))

(defun lib-osc (address &rest args)
  (coerce (gloopy.osc::%encode address args) 'list))

;;; --- OSC encoding matches the reference byte-for-byte -----------------------
(test osc-encoding-matches-reference
  (is (equal (ref-osc "/gloopy/track/5/note" 60 100)
             (lib-osc "/gloopy/track/5/note" 60 100)))
  (is (equal (ref-osc "/gloopy/track/5/cc" 74 0.5)
             (lib-osc "/gloopy/track/5/cc" 74 0.5)))
  (is (equal (ref-osc "/gloopy/track/5/vol" 0.8)
             (lib-osc "/gloopy/track/5/vol" 0.8)))
  (is (equal (ref-osc "/gloopy/transport/tempo" 128.0)
             (lib-osc "/gloopy/transport/tempo" 128.0)))
  (is (equal (ref-osc "/gloopy/transport/play")
             (lib-osc "/gloopy/transport/play"))))

;;; --- OSC type tags: ints stay ints, reals become float32 --------------------
(test osc-type-tags
  ;; note takes two int args -> ",ii"
  (let ((bytes (gloopy.osc::%encode "/gloopy/track/5/note" '(60 100))))
    (is (search (map 'vector #'char-code ",ii") bytes)))
  ;; vol takes one float arg -> ",f"
  (let ((bytes (gloopy.osc::%encode "/gloopy/track/5/vol" (list 0.8))))
    (is (search (map 'vector #'char-code ",f") bytes))))

;;; --- OSC addresses are built from ids ---------------------------------------
(test osc-addresses
  ;; %encode over a note built the way note-on does
  (flet ((addr-of (bytes)
           ;; the address is everything up to the first NUL
           (let ((nul (position 0 bytes)))
             (map 'string #'code-char (subseq bytes 0 nul)))))
    (is (string= "/gloopy/track/7/note"
                 (addr-of (gloopy.osc::%encode "/gloopy/track/7/note" '(60 100)))))
    (is (string= "/gloopy/insert/6/fx/0/param/Wet"
                 (addr-of (gloopy.osc::%encode "/gloopy/insert/6/fx/0/param/Wet"
                                               (list 0.6)))))))

;;; --- gRPC-side pure helpers -------------------------------------------------
(test grpc-service-path
  (is (string= "/gloopy.v1.Gloopy/Play" (gloopy::svc "Play")))
  (is (string= "/gloopy.v1.Gloopy/AddSynthTrack" (gloopy::svc "AddSynthTrack"))))

(test grpc-enum-maps
  (is (= 1 (gloopy::wave-int :saw)))
  (is (= 0 (gloopy::wave-int :sine)))
  (is (= 3 (gloopy::wave-int 3)))            ; ints pass through
  (signals error (gloopy::wave-int :bogus))
  (is (= 3 (gloopy::fx-int :reverb)))
  (is (= 2 (gloopy::fx-int :delay)))
  (signals error (gloopy::fx-int :bogus)))

;;; --- music theory: note names, durations, scales, chords --------------------
(test music-pitch-names
  (is (= 60 (gloopy:pitch "C4")))
  (is (= 60 (gloopy:pitch "C")))                 ; octave defaults to 4
  (is (= 61 (gloopy:pitch "C#4")))
  (is (= 61 (gloopy:pitch "Db4")))               ; enharmonic
  (is (= 0  (gloopy:pitch "C-1")))
  (is (= 127 (gloopy:pitch "G9")))
  (is (= 60 (gloopy:pitch 60)))                  ; int passes through
  (is (string= "C#4" (gloopy:pitch-name 61)))
  (is (string= "C4"  (gloopy:pitch-name 60)))
  (signals error (gloopy:pitch "H4")))

(test music-durations
  (is (= 1 (gloopy:dur "q")))
  (is (= 1/2 (gloopy:dur "8")))
  (is (= 4 (gloopy:dur "w")))
  (is (= 3/2 (gloopy:dur "q.")))                 ; dotted
  (is (= 7/4 (gloopy:dur "q..")))                ; double-dotted
  (is (= 1/3 (gloopy:dur "8t")))                 ; triplet
  (is (= 0.25 (gloopy:dur 0.25)))                ; number passes through
  (signals error (gloopy:dur "z")))

(test music-scales-chords
  (is (equal '(60 62 64 65 67 69 71) (gloopy:scale "C4" :major)))
  (is (equal '(60 63 65 67 70) (gloopy:scale "C4" :pentatonic-minor)))
  (is (= 14 (length (gloopy:scale "C4" :major 2))))
  (is (equal '(60 64 67 71) (gloopy:chord "C4" :maj7)))
  (is (equal '(60 63 67) (gloopy:chord "C4" :min)))
  (is (equal '(64 67 72) (gloopy:chord "C4" :maj 1)))   ; first inversion
  (signals error (gloopy:scale "C4" :bogus))
  (signals error (gloopy:chord "C4" :bogus)))

(test music-seq
  ;; rests advance the clock without emitting a note; 3 notes from 4 steps
  (let ((ns (gloopy:seq '(("C4" "q") ("E4" "e") (:rest "e") ("G4" "h")))))
    (is (= 3 (length ns)))
    (is (= 60 (gloopy.pb::pitch (first ns))))
    (is (= 2.0d0 (gloopy.pb::start-beat (third ns))))   ; G4 after C4(1)+E4(.5)+rest(.5)
    (is (= 2.0d0 (gloopy.pb::length-beats (third ns))))))

(test music-mini
  (flet ((rows (s) (mapcar (lambda (n) (list (gloopy.pb::pitch n)
                                             (gloopy.pb::start-beat n)
                                             (gloopy.pb::length-beats n)))
                           (gloopy:mini s))))
    ;; sticky quarter: one duration carries the whole run
    (is (equal '((60 0.0d0 1.0d0) (62 1.0d0 1.0d0) (64 2.0d0 1.0d0) (65 3.0d0 1.0d0))
               (rows "c4q d e f")))
    ;; absolute octaves survive (digits are free because durations are letters)
    (is (equal '((60 0.0d0 1.0d0) (72 1.0d0 1.0d0)) (rows "c4q c5")))
    ;; a duration change is sticky from there on
    (is (equal '((60 0.0d0 1.0d0) (67 1.0d0 2.0d0) (69 3.0d0 2.0d0))
               (rows "c4q g4h a")))
    ;; chord: every pitch shares the start and length
    (is (equal '((60 0.0d0 1.0d0) (64 0.0d0 1.0d0) (67 0.0d0 1.0d0))
               (rows "[c e g]q")))
    ;; rest inherits the running duration and advances the clock silently
    (is (equal '((60 0.0d0 0.5d0) (64 1.0d0 0.5d0)) (rows "c4e r e")))
    ;; accidentals, dotted and eighth-triplet parse (eb4 = 63, q. = 1.5)
    (is (equal '(63 0.0d0 1.5d0) (first (rows "eb4q."))))
    (is (< (abs (- (third (first (rows "c4et"))) (/ 1.0d0 3))) 1d-9))
    (signals error (gloopy:mini "[c e g"))))          ; unclosed bracket

;;; --- packages export what we advertise --------------------------------------
(test public-api-present
  (dolist (sym '("CONNECT" "DISCONNECT" "PLAY" "STOP" "ADD-SYNTH-TRACK"
                 "ADD-CLIP" "NOTE" "SEQ" "MINI" "PITCH" "PITCH-NAME" "DUR" "SCALE"
                 "CHORD" "SUBSCRIBE" "RENDER"))
    (is (eq :external (nth-value 1 (find-symbol sym :gloopy)))
        "GLOOPY should export ~a" sym))
  (dolist (sym '("CONNECT" "NOTE-ON" "NOTE-OFF" "CC" "VOL" "PAN" "MUTE"
                 "FX-PARAM" "TEMPO" "SEEK"))
    (is (eq :external (nth-value 1 (find-symbol sym :gloopy.osc)))
        "GLOOPY.OSC should export ~a" sym)))
