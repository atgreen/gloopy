;;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;;; SPDX-License-Identifier: AGPL-3.0-only
;;;;
;;;; kernel.lisp — the SBCL reference kernel for Gloopy script clips (cave #9).
;;;;
;;;; The kernel runs as an ag-grpc CLIENT of the Gloopy service on :50051 (the
;;;; interop-proven direction). Gloopy launches this image with a mode in the env:
;;;;   GLOOPY_SERVE=1   the warm kernel — long-poll KernelPoll for a job, generate,
;;;;                    POST notes back via KernelSubmit, and host a Slynk server so
;;;;                    Emacs can attach to the live image (redefine generators live).
;;;;   GLOOPY_JOB=<id>  one-shot: generate for one clip from the env context, submit, exit.
;;;;   KERNEL_SELFTEST=1  offline: run the generator and print notes (CI, no network).
;;;;
;;;; It compiles proto/gloopy.proto at startup (located via ../proto or GLOOPY_PROTO).
;;;; Launch:  sbcl --non-interactive --load common-lisp/kernel.lisp
;;;; (--script is NOT usable — it skips ~/.sbclrc, where ocicl registers ag-grpc.)

(require :asdf)
(handler-bind ((warning #'muffle-warning))
  (asdf:load-system "ag-grpc"))

;;; Compile gloopy.proto into an isolated GLOOPY.PB package (same pattern as the
;;; gloopy-pb.lisp client layer): bind *package* so nested type references resolve.
(defpackage :gloopy.pb (:use))
(let* ((here  (or *load-truename* *default-pathname-defaults*))
       (proto (or (probe-file (merge-pathnames "../proto/gloopy.proto" here))
                  (let ((e (sb-ext:posix-getenv "GLOOPY_PROTO"))) (and e (probe-file e)))
                  (error "kernel: cannot locate proto/gloopy.proto"))))
  (let ((*package* (find-package :gloopy.pb)))
    (handler-bind ((warning #'muffle-warning))
      (ag-proto:compile-proto-file (truename proto) :load t
                                   :package (find-package :gloopy.pb)))))

(defpackage :gloopy-kernel
  (:use :cl)
  (:export :main :note :set-generator :*generator*))
(in-package :gloopy-kernel)

;;; --- the script prelude: what a user's generator uses ----------------------

(defvar *generator* nil
  "The most recently registered generator: a function (or symbol) (context) -> list of
   GLOOPY.PB notes. A user script calls (set-generator #'my-fn). Kept for the cold one-shot
   path and as a fallback; the warm kernel tracks generators per clip in *generators*.")

(defvar *generators* (make-hash-table :test 'equal)
  "Warm kernel: map a clip's source-file path -> its generator, so several script clips can
   coexist in one live image without clobbering each other. Storing a *symbol* here (a named
   generator) means redefining it from Emacs (C-c C-c) is picked up live on the next generate.")

(defvar *loaded-sources* (make-hash-table :test 'equal)
  "Source paths already loaded in this session. The warm kernel loads a clip's file once, then
   trusts the live image (your interactive redefinitions) — a fresh kernel reloads from file.")

(defvar *loading-source* nil "Bound to the source path while a clip's file is being loaded.")
(defvar *last-source*    nil "The source path of the most recently generated clip.")

(defun note (pitch start length &optional (velocity 0.8d0))
  "Build a Note: MIDI PITCH, START/LENGTH in beats within the clip, VELOCITY 0..1."
  (make-instance 'gloopy.pb::note
                 :pitch (round pitch)
                 :start-beat (coerce start 'double-float)
                 :length-beats (coerce length 'double-float)
                 :velocity (coerce velocity 'single-float)))

(defun set-generator (fn)
  "Register FN (a function or the symbol of a named function) as the generator. Attributes it
   to the clip whose file is loading, or — when called interactively from Emacs — to the clip
   you most recently generated, so a live redefinition takes effect on the next generate."
  (setf *generator* fn)
  (let ((key (or *loading-source* *last-source*)))
    (when key (setf (gethash key *generators*) fn))))

;;; Default generator (used until a script loads one): an ascending diatonic run,
;;; one note per beat from the context key, deterministic in the context seed.
(defun default-generate (ctx)
  (let* ((beats (max 1 (floor (gloopy.pb::clip-len-beats ctx))))
         (k     (gloopy.pb::key-root ctx))
         (root  (+ 60 (if (>= k 0) k 0)))
         (scale #(0 2 4 5 7 9 11))
         (rng   (sb-ext:seed-random-state (logand (gloopy.pb::seed ctx) #xffffffff))))
    (loop for b below beats
          for deg = (aref scale (mod (+ b (random 3 rng)) (length scale)))
          collect (note (+ root deg) b 0.9d0))))

;;; --- bootstrap helpers -----------------------------------------------------

(defun set-parent-death-signal ()
  "On Linux, ask the OS to SIGTERM us when our parent (Gloopy) dies, so a crashed or
   hard-killed host never leaves this warm kernel orphaned and spinning on KernelPoll."
  #+linux
  (ignore-errors
    (sb-alien:alien-funcall
     (sb-alien:extern-alien "prctl"
       (function sb-alien:int sb-alien:int sb-alien:unsigned-long
                 sb-alien:unsigned-long sb-alien:unsigned-long sb-alien:unsigned-long))
     1    ; PR_SET_PDEATHSIG
     15   ; SIGTERM
     0 0 0)))

(defun free-port ()
  "Ask the OS for a free localhost TCP port."
  (let ((s (make-instance 'sb-bsd-sockets:inet-socket :type :stream :protocol :tcp)))
    (setf (sb-bsd-sockets:sockopt-reuse-address s) t)
    (sb-bsd-sockets:socket-bind s #(127 0 0 1) 0)
    (prog1 (nth-value 1 (sb-bsd-sockets:socket-name s))
      (sb-bsd-sockets:socket-close s))))

;;; Self-test: build a context, run the generator directly (no server / no network) and
;;; print the notes. Verifies proto message construction (gen-context, note) + the generator.
;;; Enable with KERNEL_SELFTEST=1.
(defun selftest ()
  (let* ((ctx   (make-instance 'gloopy.pb::gen-context :clip-len-beats 4d0 :seed 42 :key-root 0))
         (notes (handler-case (funcall (or *generator* #'default-generate) ctx)
                  (error (e) (format t "SELFTEST error: ~a~%" e) (finish-output) (sb-ext:exit :code 1)))))
    (format t "SELFTEST ok=t notes=~a~%" (length notes))
    (dolist (n notes)
      (format t "  note pitch=~a start=~a len=~a vel=~a~%"
              (gloopy.pb::pitch n) (gloopy.pb::start-beat n)
              (gloopy.pb::length-beats n) (gloopy.pb::velocity n)))
    (finish-output)
    (sb-ext:exit :code 0)))

;;; Submit mode: Gloopy launches us as an ag-grpc CLIENT of its own service (the
;;; interop-proven direction). We read the clip context from the environment, generate,
;;; and POST the notes back via the Gloopy KernelSubmit RPC, keyed by GLOOPY_JOB.
(defun env-num (name default)
  (let ((v (sb-ext:posix-getenv name)))
    (or (and v (plusp (length v)) (ignore-errors (read-from-string v))) default)))

(defun submit-job ()
  (let ((job    (sb-ext:posix-getenv "GLOOPY_JOB"))
        (port   (env-num "GLOOPY_HOST_PORT" 50051))
        (source (sb-ext:posix-getenv "GLOOPY_SOURCE"))
        (ok t) (err "") (notes '()))
    (handler-case
        (let ((ctx (make-instance 'gloopy.pb::gen-context
                                  :tempo-bpm (coerce (env-num "GLOOPY_CTX_TEMPO" 120) 'double-float)
                                  :clip-len-beats (coerce (env-num "GLOOPY_CTX_LEN" 4) 'double-float)
                                  :seed (env-num "GLOOPY_CTX_SEED" 0)
                                  :key-root (env-num "GLOOPY_CTX_KEY" -1))))
          (when (and source (plusp (length source)))
            (let ((*package* (find-package :gloopy-kernel))) (load (truename source))))
          (setf notes (funcall (or *generator* #'default-generate) ctx)))
      (error (e) (setf ok nil err (format nil "~a" e))))
    (let ((channel (ag-grpc:make-channel "127.0.0.1" port)))
      (unwind-protect
          (ag-grpc:call-unary channel "/gloopy.v1.Gloopy/KernelSubmit"
                              (make-instance 'gloopy.pb::kernel-submit-request
                                             :job job :ok ok :notes notes :error err)
                              :response-type 'gloopy.pb::ack)
        (ag-grpc:channel-close channel)))
    (sb-ext:exit :code (if ok 0 1))))

;;; Load a clip's source file into the live image, but only the FIRST time we see it this
;;; session. After that we trust the running image: your interactive redefinitions from Emacs
;;; (C-c C-c) are what generate uses. A fresh kernel has an empty *loaded-sources*, so it
;;; reloads from file — which is what keeps a project reproducible after a restart.
(defun ensure-source-loaded (source)
  (when (and source (plusp (length source)) (not (gethash source *loaded-sources*)))
    (let ((*loading-source* source)
          (*package* (find-package :gloopy-kernel)))
      (load (truename source)))
    (setf (gethash source *loaded-sources*) t)))

;;; Serve mode (the warm kernel): stay resident and long-poll Gloopy for generate jobs, so the
;;; proto compiles once at startup and every generate after is instant. The image is LIVE — a
;;; clip's file loads once, then Slynk-driven redefinitions (Emacs) drive subsequent generates.
;;; Each clip's generator is tracked per source in *generators*, so multiple script clips don't
;;; clobber each other.
(defun process-job (channel spec)
  (let ((ctx (make-instance 'gloopy.pb::gen-context
                            :tempo-bpm (gloopy.pb::tempo-bpm spec)
                            :clip-len-beats (gloopy.pb::clip-len-beats spec)
                            :seed (gloopy.pb::seed spec)
                            :key-root (gloopy.pb::key-root spec)))
        (source (gloopy.pb::source spec))
        (ok t) (err "") (notes '()))
    (handler-case
        (progn
          (ensure-source-loaded source)
          (when (and source (plusp (length source)))
            (setf *last-source* source))               ; so interactive (set-generator ...) attributes here
          (let ((gen (or (and source (plusp (length source)) (gethash source *generators*))
                         *generator*
                         #'default-generate)))
            (setf notes (funcall gen ctx))))           ; a symbol funcalls its LIVE definition
      (error (e) (setf ok nil err (format nil "~a" e))))
    (ignore-errors
      (ag-grpc:call-unary channel "/gloopy.v1.Gloopy/KernelSubmit"
                          (make-instance 'gloopy.pb::kernel-submit-request
                                         :job (gloopy.pb::job spec) :ok ok :notes notes :error err)
                          :response-type 'gloopy.pb::ack))))

(defun serve ()
  (set-parent-death-signal)   ; die with Gloopy — never outlive a crashed host
  (let* ((port       (env-num "GLOOPY_HOST_PORT" 50051))
         (channel    (ag-grpc:make-channel "127.0.0.1" port))
         (slynk-port 0))
    ;; Start Slynk so Emacs (Sly) can attach to this warm image; report the port to Gloopy.
    (handler-case
        (progn
          (handler-bind ((warning #'muffle-warning)) (require :asdf) (asdf:load-system :slynk))
          (setf slynk-port (free-port))
          (funcall (find-symbol "CREATE-SERVER" "SLYNK") :port slynk-port :dont-close t))
      (error (e) (setf slynk-port 0) (format *error-output* "slynk: ~a~%" e)))
    (ignore-errors
      (ag-grpc:call-unary channel "/gloopy.v1.Gloopy/KernelReady"
                          (make-instance 'gloopy.pb::kernel-ready-request :slynk-port slynk-port)
                          :response-type 'gloopy.pb::ack))
    (loop
      (let ((spec (ignore-errors
                    (ag-grpc:call-response
                     (ag-grpc:call-unary channel "/gloopy.v1.Gloopy/KernelPoll"
                                         (make-instance 'gloopy.pb::kernel-poll-request :lang "common-lisp")
                                         :response-type 'gloopy.pb::kernel-job-spec)))))
        (if (and spec (plusp (length (gloopy.pb::job spec))))
            (process-job channel spec)
            (sleep 0.2))))))       ; empty poll (timeout / transient) — brief backoff, re-poll

;;; Entry point. Launch with:  sbcl --non-interactive --load common-lisp/kernel.lisp
;;; (--script is NOT usable — it skips ~/.sbclrc, where ocicl registers ag-grpc.)
;;; GLOOPY_SERVE -> warm kernel (serves jobs + hosts Slynk); GLOOPY_JOB -> one-shot submit;
;;; KERNEL_SELFTEST -> offline self-test.
(cond ((sb-ext:posix-getenv "KERNEL_SELFTEST") (selftest))
      ((sb-ext:posix-getenv "GLOOPY_SERVE")    (serve))       ; warm kernel: long-poll for jobs + Slynk
      ((sb-ext:posix-getenv "GLOOPY_JOB")      (submit-job))
      (t (format *error-output*
                 "kernel: no mode set (expected GLOOPY_SERVE, GLOOPY_JOB, or KERNEL_SELFTEST)~%")
         (sb-ext:exit :code 2)))
