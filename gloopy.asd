;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: GPL-3.0-only

;;;; gloopy.asd — Common Lisp client for the Gloopy DAW's gRPC control API.
;;;;
;;;; Prerequisite (once):  ocicl install ag-grpc
;;;; Then, with sbcl started from the repo root:
;;;;     (asdf:load-system :gloopy)
;;;;     (gloopy:connect)
;;;;
;;;; :serial t matters — gloopy-pb compiles the .proto into the GLOOPY.PB package
;;;; at load time, and must be loaded before gloopy-grpc (which references those
;;;; generated classes) is compiled.

(asdf:defsystem "gloopy"
  :description "Common Lisp client for the Gloopy DAW's gRPC control API."
  :author "Anthony Green <green@moxielogic.com>"
  :license "GPL-3.0-only"
  :version "0.1.0"
  :depends-on ("ag-grpc")
  :serial t
  :pathname "examples"
  :components ((:file "gloopy-pb")
               (:file "gloopy-grpc")))
