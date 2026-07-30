;;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;;; SPDX-License-Identifier: AGPL-3.0-only
;;;;
;;;; kernel.lisp — the SBCL reference kernel for Gloopy script clips (cave #9).
;;;;
;;;; Implements the gloopy.v1.Kernel gRPC service (see proto/gloopy.proto): Gloopy
;;;; launches this image and drives it — LoadSource loads/redefines a user script
;;;; into the warm image, Generate calls the script's generator with a context and
;;;; returns notes, Describe reports the runtime. Gloopy is the client; this is the
;;;; server. The reverse direction (reading project state) reuses the existing
;;;; Gloopy service via the gloopy-grpc.lisp client — not needed for Generate.
;;;;
;;;; On start it prints "KERNEL-PORT <n>" on stdout so the host can discover the
;;;; listening port. Launch:  sbcl --script common-lisp/kernel.lisp [PORT]

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
  "The current generator: a function (context) -> list of GLOOPY.PB notes.
   A user script calls (set-generator #'my-fn) from LoadSource.")

(defun note (pitch start length &optional (velocity 0.8d0))
  "Build a Note: MIDI PITCH, START/LENGTH in beats within the clip, VELOCITY 0..1."
  (make-instance 'gloopy.pb::note
                 :pitch (round pitch)
                 :start-beat (coerce start 'double-float)
                 :length-beats (coerce length 'double-float)
                 :velocity (coerce velocity 'single-float)))

(defun set-generator (fn) (setf *generator* fn))

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

;;; --- gRPC handlers (each: (request context) -> response message) -----------

(defun handle-describe (req ctx)
  (declare (ignore req ctx))
  (make-instance 'gloopy.pb::kernel-info
                 :language "common-lisp"
                 :runtime (format nil "SBCL ~a" (lisp-implementation-version))
                 :version "0.1"
                 :capabilities (list "generate" "load-source")))

(defun handle-load-source (req ctx)
  (declare (ignore ctx))
  (let ((path (gloopy.pb::path req))
        (src  (gloopy.pb::source req))
        (diags '()))
    (handler-case
        (let ((*package* (find-package :gloopy-kernel)))
          (cond ((and src (plusp (length src)))
                 (with-input-from-string (in src)
                   (loop for form = (read in nil :eof) until (eq form :eof) do (eval form))))
                ((and path (plusp (length path)))
                 (load (truename path)))))
      (error (e)
        (push (make-instance 'gloopy.pb::diagnostic :severity 2
                             :message (format nil "~a" e)) diags)))
    (make-instance 'gloopy.pb::load-result
                   :ok (null diags) :diagnostics (nreverse diags))))

(defun handle-generate (req ctx)
  (declare (ignore ctx))
  (handler-case
      (let* ((context (gloopy.pb::context req))
             (notes   (funcall (or *generator* #'default-generate) context)))
        (make-instance 'gloopy.pb::gen-result :ok t :notes notes))
    (error (e)
      (make-instance 'gloopy.pb::gen-result :ok nil
                     :diagnostics (list (make-instance 'gloopy.pb::diagnostic :severity 2
                                                       :message (format nil "~a" e)))))))

;;; --- server bootstrap ------------------------------------------------------

(defun free-port ()
  "Ask the OS for a free localhost TCP port."
  (let ((s (make-instance 'sb-bsd-sockets:inet-socket :type :stream :protocol :tcp)))
    (setf (sb-bsd-sockets:sockopt-reuse-address s) t)
    (sb-bsd-sockets:socket-bind s #(127 0 0 1) 0)
    (prog1 (nth-value 1 (sb-bsd-sockets:socket-name s))
      (sb-bsd-sockets:socket-close s))))

(defun main (&optional port)
  (let* ((port   (or port (free-port)))
         (server (ag-grpc:make-grpc-server port :host "127.0.0.1")))
    (ag-grpc:server-register-handler server "/gloopy.v1.Kernel/Describe"   #'handle-describe
                                     :request-type 'gloopy.pb::empty        :response-type 'gloopy.pb::kernel-info)
    (ag-grpc:server-register-handler server "/gloopy.v1.Kernel/LoadSource" #'handle-load-source
                                     :request-type 'gloopy.pb::load-request :response-type 'gloopy.pb::load-result)
    (ag-grpc:server-register-handler server "/gloopy.v1.Kernel/Generate"   #'handle-generate
                                     :request-type 'gloopy.pb::gen-request  :response-type 'gloopy.pb::gen-result)
    (format t "KERNEL-PORT ~a~%" port)
    (finish-output)
    ;; Robust handshake: if the host passed a port-file path, write the port there
    ;; (atomically) so it doesn't have to parse our stdout past the proto-compile output.
    (let ((pf (sb-ext:posix-getenv "GLOOPY_KERNEL_PORTFILE")))
      (when pf
        (let ((tmp (concatenate 'string pf ".tmp")))
          (with-open-file (o tmp :direction :output :if-exists :supersede :if-does-not-exist :create)
            (format o "~a~%" port))
          (rename-file tmp pf))))
    (ag-grpc:server-start server)))

;;; Self-test: exercise the Generate handler with a synthetic request (no server /
;;; no network) and print the notes. Verifies proto message construction + the
;;; generator + the handler. Enable with KERNEL_SELFTEST=1.
(defun selftest ()
  (let* ((ctx (make-instance 'gloopy.pb::gen-context :clip-len-beats 4d0 :seed 42 :key-root 0))
         (req (make-instance 'gloopy.pb::gen-request :context ctx))
         (res (handle-generate req nil))
         (ok  (gloopy.pb::ok res)))
    (format t "SELFTEST ok=~a notes=~a~%" ok (length (gloopy.pb::notes res)))
    (dolist (n (gloopy.pb::notes res))
      (format t "  note pitch=~a start=~a len=~a vel=~a~%"
              (gloopy.pb::pitch n) (gloopy.pb::start-beat n)
              (gloopy.pb::length-beats n) (gloopy.pb::velocity n)))
    (finish-output)
    (sb-ext:exit :code (if ok 0 1))))

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

;;; SWANK mode (cave #15): a persistent, warm image with a SWANK server so you can attach
;;; SLIME/Sly and inspect/redefine/restart generators interactively (the prelude — note,
;;; set-generator, default-generate — is already loaded). Writes the SWANK port to
;;; GLOOPY_KERNEL_PORTFILE so Gloopy can show it, then stays alive.
(defun swank-repl ()
  (handler-bind ((warning #'muffle-warning)) (require :asdf) (asdf:load-system :swank))
  (let ((port (free-port))
        (pf   (sb-ext:posix-getenv "GLOOPY_KERNEL_PORTFILE")))
    (funcall (find-symbol "CREATE-SERVER" "SWANK") :port port :dont-close t)
    (when pf
      (let ((tmp (concatenate 'string pf ".tmp")))
        (with-open-file (o tmp :direction :output :if-exists :supersede :if-does-not-exist :create)
          (format o "~a~%" port))
        (rename-file tmp pf)))
    (format t "SWANK-PORT ~a~%" port) (finish-output)
    (loop (sleep 3600))))

;;; Entry point. Launch with:  sbcl --non-interactive --load common-lisp/kernel.lisp
;;; (--script is NOT usable — it skips ~/.sbclrc, where ocicl registers ag-grpc.)
;;; GLOOPY_SWANK -> SWANK REPL; GLOOPY_JOB -> submit mode; KERNEL_SELFTEST -> self-test; else serve.
(cond ((sb-ext:posix-getenv "KERNEL_SELFTEST") (selftest))
      ((sb-ext:posix-getenv "GLOOPY_SWANK")    (swank-repl))
      ((sb-ext:posix-getenv "GLOOPY_JOB")      (submit-job))
      (t (main (let ((p (sb-ext:posix-getenv "KERNEL_PORT"))) (and p (parse-integer p :junk-allowed t))))))
