;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: AGPL-3.0-only

;;;; packages.lisp — package definitions for the Gloopy client library.
;;;;
;;;; Two user-facing packages, one per control lane:
;;;;
;;;;   GLOOPY      — the gRPC structural lane.  The authoritative surface:
;;;;                 create things, query state, edit the model, render.
;;;;   GLOOPY.OSC  — the OSC live lane (nickname GLOSC).  Fire-and-forget
;;;;                 real-time notes/CC/knobs; performs on ids GLOOPY created.
;;;;
;;;; The generated protobuf package GLOOPY.PB is created in proto.lisp (it must
;;;; NOT :use :cl, so it can't be declared alongside these).

(in-package :cl-user)

(defpackage :gloopy
  (:use :cl)
  (:documentation
   "Client for Gloopy's gRPC structural control lane (127.0.0.1:50051).
Every RPC is a plain function; messages come back as plists.")
  (:export
   ;; connection
   #:connect #:disconnect #:*channel* #:connectedp
   ;; transport
   #:play #:stop #:set-tempo #:set-swing #:seek #:transport
   ;; tempo map
   #:add-tempo-marker #:remove-tempo-marker #:list-tempo-markers
   ;; tracks
   #:list-tracks #:get-state #:add-synth-track #:add-sampler-track #:add-sfz-track
   #:add-audio-track #:add-plugin-track #:set-track-params #:set-synth-param
   #:remove-track
   ;; macros (the rack layer)
   #:add-macro #:set-macro-value #:map-macro-synth #:map-macro-effect #:randomize-macros
   ;; clips
   #:note #:seq #:mini #:add-clip #:remove-clip #:move-clip #:add-audio-clip
   ;; music theory (note names, durations, scales, chords)
   #:pitch #:pitch-name #:dur #:scale #:chord
   ;; mixer / effects
   #:list-inserts #:add-effect #:add-plugin-effect #:remove-effect
   #:set-effect-param #:set-effect-bypass #:effect-params
   ;; plugins
   #:list-plugins #:scan-plugins #:open-plugin-editor
   ;; project / render
   #:new-project #:load-project #:save-project
   #:save-composition #:load-composition #:render
   ;; events
   #:subscribe))

(defpackage :gloopy.osc
  (:use :cl)
  (:nicknames :glosc)
  (:documentation
   "Client for Gloopy's OSC live performance lane (127.0.0.1:9000, UDP).
Low-latency, fire-and-forget: notes, CC, knob turns, transport.  Built on the
third-party OSC library.  Perform on ids that GLOOPY created.")
  (:export
   ;; connection / lifecycle
   #:connect #:disconnect #:connectedp #:with-osc
   #:*host* #:*port* #:*transmitter*
   ;; live MIDI
   #:note-on #:note-off #:chord #:cc
   ;; track params (atomic lane)
   #:vol #:pan #:mute
   ;; effects (atomic lane)
   #:fx-param
   ;; transport (fire-and-forget)
   #:play #:stop #:tempo #:seek
   ;; a tiny built-in demo
   #:demo))
