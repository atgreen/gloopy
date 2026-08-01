;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: AGPL-3.0-only

;;;; music.lisp — music-theory sugar for building Gloopy clips.
;;;;
;;;; Pure helpers (no server, no gRPC): turn musician vocabulary into the ints
;;;; and beats the wire format wants.  A pitch is a MIDI number (C4 = 60, the
;;;; middle C Gloopy uses for a sampler root); a duration is a length in beats
;;;; (a quarter note = 1 beat).  #'note, #'seq and #'add-clip (in grpc.lisp)
;;;; consume these.
;;;;
;;;;   (add-clip tid :notes (seq '(("C4" "q") ("E4" "e") (:rest "e") ("G4" "h"))))
;;;;   (chord "C4" :maj7)        ; => (60 64 67 71)
;;;;   (scale "C4" :major)       ; => (60 62 64 65 67 69 71)
;;;;
;;;; Scale names mirror the engine (../../Source/Scales.cpp) so the desktop
;;;; "set scale" control and both client libraries speak one dialect.

(in-package :gloopy)

;;; --- note names <-> MIDI ----------------------------------------------------
;;; Scientific pitch notation: C4 = 60, so MIDI 0 = C-1 and 127 = G9.

(defparameter +pitch-classes+ '((#\c . 0) (#\d . 2) (#\e . 4) (#\f . 5)
                                (#\g . 7) (#\a . 9) (#\b . 11)))
(defparameter +sharp-names+ #("C" "C#" "D" "D#" "E" "F" "F#" "G" "G#" "A" "A#" "B"))

(defun pitch (p)
  "MIDI number for a note name (\"C4\", \"F#3\", \"Bb5\"); passes integers through.
Accidentals may repeat (\"C##4\"); octave is optional, default 4 (so \"C\" is
middle C)."
  (etypecase p
    (integer p)
    ((or string symbol)
     (let* ((s (string-trim '(#\Space) (string p)))
            (len (length s)))
       (when (zerop len) (error "empty pitch name"))
       (let ((base (cdr (assoc (char-downcase (char s 0)) +pitch-classes+))))
         (unless base (error "bad note name: ~A" p))
         (let ((semis base) (i 1))
           (loop while (and (< i len) (member (char s i) '(#\# #\b)))
                 do (incf semis (if (char= (char s i) #\#) 1 -1)) (incf i))
           (let ((octave (if (< i len) (parse-integer s :start i) 4)))
             (+ (* (1+ octave) 12) semis))))))))

(defun pitch-name (midi)
  "Sharp-spelled scientific name for a MIDI number (61 => \"C#4\")."
  (format nil "~A~D" (aref +sharp-names+ (mod midi 12)) (1- (floor midi 12))))

;;; --- durations --------------------------------------------------------------
;;; Length in beats, quarter note = 1.  A trailing "." dots (x1.5, stackable);
;;; a trailing "t" makes a triplet (x2/3).

(defparameter +durations+
  '(("w" . 4) ("whole" . 4) ("h" . 2) ("half" . 2)
    ("q" . 1) ("quarter" . 1) ("e" . 1/2) ("eighth" . 1/2) ("8" . 1/2)
    ("s" . 1/4) ("sixteenth" . 1/4) ("16" . 1/4)
    ("32" . 1/8) ("thirtysecond" . 1/8) ("64" . 1/16)))

(defun dur (d)
  "Length in beats for a duration shorthand; passes numbers through.
(dur \"q\") => 1, (dur \"8\") => 1/2, (dur \"q.\") => 3/2 (dotted),
(dur \"8t\") => 1/3 (triplet).  Dots stack: \"q..\" => 7/4."
  (if (numberp d)
      d
      (let* ((s (string-downcase (string-trim '(#\Space) (string d))))
             (triplet (and (plusp (length s)) (char= (char s (1- (length s))) #\t))))
        (when triplet (setf s (subseq s 0 (1- (length s)))))
        (let ((dots 0))
          (loop while (and (plusp (length s)) (char= (char s (1- (length s))) #\.))
                do (incf dots) (setf s (subseq s 0 (1- (length s)))))
          (let ((base (cdr (assoc s +durations+ :test #'string=))))
            (unless base (error "bad duration: ~A" d))
            (let ((beats base) (add base))
              (dotimes (_ dots) (setf add (/ add 2)) (incf beats add))
              (when triplet (setf beats (* beats 2/3)))
              beats))))))

;;; --- scales -----------------------------------------------------------------
;;; Semitone offsets from the root.  Mirrors Source/Scales.cpp exactly.

(defparameter +scales+
  '(("major" 0 2 4 5 7 9 11) ("ionian" 0 2 4 5 7 9 11)
    ("minor" 0 2 3 5 7 8 10) ("aeolian" 0 2 3 5 7 8 10)
    ("harmonic-minor" 0 2 3 5 7 8 11) ("melodic-minor" 0 2 3 5 7 9 11)
    ("dorian" 0 2 3 5 7 9 10) ("phrygian" 0 1 3 5 7 8 10)
    ("lydian" 0 2 4 6 7 9 11) ("mixolydian" 0 2 4 5 7 9 10)
    ("locrian" 0 1 3 5 6 8 10)
    ("pentatonic-major" 0 2 4 7 9) ("pentatonic-minor" 0 3 5 7 10)
    ("blues" 0 3 5 6 7 10) ("whole-tone" 0 2 4 6 8 10)
    ("chromatic" 0 1 2 3 4 5 6 7 8 9 10 11)))

(defun scale (root &optional (name :major) (octaves 1))
  "MIDI pitches of a scale, ascending from ROOT over OCTAVES.
(scale \"C4\" :major) => (60 62 64 65 67 69 71).  Names match the engine
(:major :dorian :pentatonic-minor :blues ...)."
  (let ((steps (cdr (assoc (string-downcase (string name)) +scales+ :test #'string=))))
    (unless steps (error "unknown scale: ~A" name))
    (let ((base (pitch root)))
      (loop for o below octaves
            nconc (loop for s in steps collect (+ base (* 12 o) s))))))

;;; --- chords -----------------------------------------------------------------
;;; Semitone offsets from the root.  Aliases cover the usual spellings.

(defparameter +chords+
  '(("maj" 0 4 7) ("major" 0 4 7) ("" 0 4 7)
    ("min" 0 3 7) ("minor" 0 3 7) ("m" 0 3 7)
    ("dim" 0 3 6) ("aug" 0 4 8) ("sus2" 0 2 7) ("sus4" 0 5 7)
    ("maj7" 0 4 7 11) ("min7" 0 3 7 10) ("m7" 0 3 7 10)
    ("7" 0 4 7 10) ("dom7" 0 4 7 10)
    ("dim7" 0 3 6 9) ("m7b5" 0 3 6 10) ("half-dim" 0 3 6 10)
    ("6" 0 4 7 9) ("min6" 0 3 7 9) ("m6" 0 3 7 9)
    ("9" 0 4 7 10 14) ("maj9" 0 4 7 11 14) ("min9" 0 3 7 10 14)
    ("add9" 0 4 7 14) ("5" 0 7)))

(defun chord (root &optional (quality :maj) (inversion 0))
  "MIDI pitches of a chord.  (chord \"C4\" :maj7) => (60 64 67 71).
INVERSION raises that many of the lowest notes by an octave.  Qualities:
:maj :min :dim :aug :sus2 :sus4 :maj7 :min7 :7 :6 :9 ..."
  (let ((offs (cdr (assoc (string-downcase (string quality)) +chords+ :test #'string=))))
    (unless offs (error "unknown chord: ~A" quality))
    (let ((notes (loop for s in offs collect (+ (pitch root) s))))
      (dotimes (_ inversion)
        (setf notes (append (rest notes) (list (+ (first notes) 12)))))
      notes)))
