;;; SPDX-FileCopyrightText: 2026 Anthony Green <anthony@atgreen.org>
;;; SPDX-License-Identifier: GPL-3.0-only

;;;; gloopy-pb.lisp — the proto layer for the Gloopy gRPC client.
;;;;
;;;; Compiles gloopy.proto at load time into an isolated GLOOPY.PB package that
;;;; does NOT :use :cl.  Two reasons:
;;;;   * proto fields sharing a name with a locked CL symbol (Param's min/max)
;;;;     would otherwise clash when interned;
;;;;   * ag-proto's codegen qualifies message *classes* with the target package
;;;;     but interns nested-field type references (in repeated-field
;;;;     deserializers) via *package* — so we bind *package* to GLOOPY.PB too,
;;;;     making the two agree.
;;;;
;;;; Loaded automatically by the :gloopy ASDF system and by gloopy-grpc.lisp; you
;;;; don't normally load this file directly.

;;; When loaded on its own (not via the :gloopy system), pull in the gRPC stack.
(eval-when (:load-toplevel :execute)
  (unless (find-package :ag-proto)
    (require :asdf)
    (asdf:load-system "ag-grpc")))

(defpackage :gloopy.pb (:use))

(eval-when (:load-toplevel :execute)
  (let ((proto (or (ignore-errors
                     (asdf:system-relative-pathname "gloopy" "proto/gloopy.proto"))
                   (merge-pathnames "../proto/gloopy.proto"
                                    (or *load-truename* *default-pathname-defaults*)))))
    (unless (and proto (probe-file proto))
      (error "gloopy.pb: can't locate proto/gloopy.proto (looked at ~a)" proto))
    ;; Bind *package* so nested-message type references resolve into GLOOPY.PB.
    (let ((*package* (find-package :gloopy.pb)))
      (ag-proto:compile-proto-file proto :load t :package (find-package :gloopy.pb)))))
