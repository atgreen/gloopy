;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: AGPL-3.0-only

;;;; gloopy.asd — Common Lisp client library for the Gloopy DAW.
;;;;
;;;; Gloopy exposes two control lanes (see ../docs/CONTROL-API.md):
;;;;
;;;;   * gRPC  (127.0.0.1:50051) — the structural lane: create/remove tracks,
;;;;     schedule clips, host plugins, add effects, load/save, bounce to WAV,
;;;;     stream the playhead + meters.  This library wraps it in the GLOOPY
;;;;     package, one plain function per RPC, everything in/out as plists.
;;;;
;;;;   * OSC   (127.0.0.1:9000, UDP) — the live performance lane: note on/off,
;;;;     CC, real-time knob turns, fire-and-forget transport.  Wrapped in the
;;;;     GLOOPY.OSC package (nickname GLOSC), built on the third-party `osc'
;;;;     library rather than a hand-rolled encoder.
;;;;
;;;; Dependencies are vendored with ocicl (see ocicl.csv); the key one is
;;;; ag-grpc (which brings ag-proto — the .proto is compiled to Lisp at load
;;;; time).  From a checkout, run sbcl in THIS directory so ocicl and the cwd
;;;; source-registry are on the path:
;;;;
;;;;     (asdf:load-system :gloopy)
;;;;     (gloopy:connect)              ; structural lane
;;;;     (glosc:open)                  ; live lane
;;;;
;;;; :serial t matters — src/proto compiles ../proto/gloopy.proto into the
;;;; GLOOPY.PB package at load time and must precede src/grpc (which names those
;;;; generated classes at compile time).

(asdf:defsystem "gloopy"
  :description "Common Lisp client for the Gloopy DAW (gRPC structural + OSC live lanes)."
  :author "Anthony Green <green@moxielogic.com>"
  :license "AGPL-3.0-only"
  :version "0.1.0"
  :depends-on ("ag-grpc" "osc" "usocket")
  :serial t
  :pathname "src"
  :components ((:file "packages")
               (:file "proto")
               (:file "grpc")
               (:file "osc"))
  :in-order-to ((asdf:test-op (asdf:test-op "gloopy/tests"))))

(asdf:defsystem "gloopy/tests"
  :description "Offline tests for the Gloopy Common Lisp client (no server needed)."
  :author "Anthony Green <green@moxielogic.com>"
  :license "AGPL-3.0-only"
  :depends-on ("gloopy" "fiveam")
  :serial t
  :pathname "tests"
  :components ((:file "tests"))
  :perform (asdf:test-op (o c)
             (uiop:symbol-call :fiveam :run! (uiop:find-symbol* :gloopy :gloopy-tests))))
