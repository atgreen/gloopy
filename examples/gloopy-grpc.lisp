;;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;;; SPDX-License-Identifier: AGPL-3.0-only

;;;; gloopy-grpc.lisp — drive Gloopy's gRPC control surface from Common Lisp.
;;;;
;;;; The gRPC lane is the *structural* half of Gloopy's control API: create and
;;;; remove tracks, hand over MIDI sequences, add and tweak effects, host
;;;; plugins, load/save projects, bounce to WAV, and stream the playhead + meters
;;;; back out.  (Live, low-latency notes/knobs are on the OSC lane — see
;;;; gloopy-osc.lisp.)  Gloopy listens on 127.0.0.1:50051.
;;;;
;;;; Setup (once, from the repo root):
;;;;     ocicl install ag-grpc
;;;;
;;;; Usage — as an ASDF system (run sbcl from the repo root):
;;;;     (asdf:load-system :gloopy)
;;;;     (in-package :gloopy)
;;;;     (connect)                                  ; 127.0.0.1:50051
;;;;
;;;; ...or load this file directly:
;;;;     (load "examples/gloopy-grpc.lisp")
;;;;     (in-package :gloopy)
;;;;     (connect)
;;;;     (get-state)                                ; => a snapshot plist
;;;;     (defparameter id (add-synth-track "Lead" :wave :saw :release 0.3))
;;;;     (add-clip id :length 4 :notes (list (note 60 0 1) (note 64 1 1)
;;;;                                          (note 67 2 1) (note 72 3 1)))
;;;;     (play)
;;;;     (subscribe :seconds 3)                     ; stream playhead + meters
;;;;     (stop)
;;;;     (render "/tmp/mix.wav")                    ; offline bounce
;;;;
;;;; The .proto is compiled to Lisp at load time by ag-proto, into a dedicated
;;;; GLOOPY.PB package that does NOT :use :cl — so proto fields that share a name
;;;; with a locked CL symbol (Param's min/max) can't collide.  This wrapper hides
;;;; all of that: everything comes back as plain plists.

;;; The proto layer (GLOOPY.PB package) is a separate component; the :gloopy
;;; system loads it first.  When this file is loaded on its own, pull it in
;;; (it, in turn, loads ag-grpc and compiles the proto).
(eval-when (:load-toplevel :execute)
  (unless (find-package :gloopy.pb)
    (load (merge-pathnames "gloopy-pb.lisp"
                           (or *load-truename* *default-pathname-defaults*)))))

;;; --- the friendly client API ------------------------------------------------
(defpackage :gloopy
  (:use :cl)
  (:export :connect :disconnect :*channel*
           ;; transport
           :play :stop :set-tempo :set-swing :seek :transport
           ;; tracks
           :list-tracks :get-state :add-synth-track :add-sampler-track :add-sfz-track
           :add-audio-track :add-plugin-track :set-track-params :set-synth-param
           :remove-track
           ;; clips
           :note :add-clip :remove-clip :move-clip :add-audio-clip
           ;; mixer / effects
           :list-inserts :add-effect :add-plugin-effect :remove-effect
           :set-effect-param :set-effect-bypass :effect-params
           ;; plugins
           :list-plugins :scan-plugins :open-plugin-editor
           ;; project / render
           :new-project :load-project :save-project :save-composition :load-composition :render
           ;; events
           :subscribe))
(in-package :gloopy)

(defvar *channel* nil "The active ag-grpc channel, or NIL if not connected.")

;;; --- plumbing ---------------------------------------------------------------
(defun svc (method) (concatenate 'string "/gloopy.v1.Gloopy/" method))
(defun d (x) (coerce x 'double-float))     ; proto double fields
(defun s (x) (coerce x 'single-float))     ; proto float fields
(defun mk (class &rest initargs) (apply #'make-instance class initargs))

(defparameter *waveforms*    '(:sine 0 :saw 1 :square 2 :triangle 3))
(defparameter *effect-types* '(:gain 0 :filter 1 :delay 2 :reverb 3))
(defun wave-int (w) (if (integerp w) w (or (getf *waveforms* w)    (error "unknown waveform ~s" w))))
(defun fx-int   (ty)(if (integerp ty)ty (or (getf *effect-types* ty)(error "unknown effect type ~s" ty))))

(defun %unary (method request response-type)
  "Make a unary call, signalling on a non-OK status; returns the response message."
  (unless *channel* (error "Gloopy: not connected — call (connect) first."))
  (let* ((call (ag-grpc:call-unary *channel* (svc method) request :response-type response-type))
         (status (ag-grpc:call-status call)))
    (unless (eql status 0)
      (error "Gloopy RPC ~a failed (status ~a): ~a" method status
             (ignore-errors (ag-grpc::call-status-message call))))
    (ag-grpc:call-response call)))

(defun %ack (method request)
  "Unary call returning Ack; T on success, else signal with the server's message."
  (let ((r (%unary method request 'gloopy.pb::ack)))
    (or (gloopy.pb::ok r)
        (error "Gloopy: ~a rejected: ~a" method (gloopy.pb::proto-ack-error r)))))

;;; --- connection -------------------------------------------------------------
(defun connect (&key (host "127.0.0.1") (port 50051))
  "Open (or reopen) the channel to a running Gloopy."
  (when *channel* (ignore-errors (ag-grpc:channel-close *channel*)))
  (setf *channel* (ag-grpc:make-channel host port))
  (format t "~&Connected to Gloopy at ~a:~a~%" host port)
  *channel*)

(defun disconnect ()
  (when *channel* (ag-grpc:channel-close *channel*) (setf *channel* nil))
  (values))

;;; --- transport --------------------------------------------------------------
(defun play      () (%ack "Play" (mk 'gloopy.pb::empty)))
(defun stop      () (%ack "Stop" (mk 'gloopy.pb::empty)))
(defun set-tempo (bpm)   (%ack "SetTempo" (mk 'gloopy.pb::tempo :bpm (d bpm))))
(defun set-swing (amount) ; 0.5 straight … 0.75 triplet feel
  (%ack "SetSwing" (mk 'gloopy.pb::swing :amount (d amount))))
(defun seek      (beats) (%ack "Seek" (mk 'gloopy.pb::proto-position :beats (d beats))))

(defun transport ()
  "Current transport state as a plist (:playing :bpm :position-beats)."
  (let ((r (%unary "GetTransport" (mk 'gloopy.pb::empty) 'gloopy.pb::transport-state)))
    (list :playing (gloopy.pb::playing r) :bpm (gloopy.pb::bpm r)
          :position-beats (gloopy.pb::position-beats r))))

;;; --- conversions ------------------------------------------------------------
(defun track->plist (ti)
  (list :id (gloopy.pb::id ti) :name (gloopy.pb::name ti)
        :type (gloopy.pb::proto-track-info-type ti) :volume (gloopy.pb::volume ti)
        :pan (gloopy.pb::pan ti) :mute (gloopy.pb::mute ti) :clips (gloopy.pb::clips ti)))

(defun effect->plist (ei)
  (list :slot (gloopy.pb::slot ei) :name (gloopy.pb::name ei)
        :bypassed (gloopy.pb::bypassed ei)))

(defun insert->plist (mi)
  (list :index (gloopy.pb::index mi) :name (gloopy.pb::name mi)
        :volume (gloopy.pb::volume mi) :pan (gloopy.pb::pan mi)
        :mute (gloopy.pb::mute mi) :solo (gloopy.pb::solo mi)
        :effects (mapcar #'effect->plist (gloopy.pb::effects mi))))

(defun plugin->plist (pg)
  (list :name (gloopy.pb::name pg) :format (gloopy.pb::proto-plugin-info-format pg)
        :instrument (gloopy.pb::is-instrument pg) :identifier (gloopy.pb::identifier pg)))

;;; --- tracks -----------------------------------------------------------------
(defun list-tracks ()
  "List of track plists (:id :name :type :volume :pan :mute :clips)."
  (mapcar #'track->plist
          (gloopy.pb::tracks (%unary "ListTracks" (mk 'gloopy.pb::empty) 'gloopy.pb::track-list))))

(defun get-state ()
  "Whole-project snapshot: (:transport <plist> :tracks (...) :inserts (...))."
  (let* ((st (%unary "GetState" (mk 'gloopy.pb::empty) 'gloopy.pb::project-state))
         (tr (gloopy.pb::transport st)))
    (list :transport (list :playing (gloopy.pb::playing tr) :bpm (gloopy.pb::bpm tr)
                           :position-beats (gloopy.pb::position-beats tr))
          :tracks  (mapcar #'track->plist (gloopy.pb::tracks st))
          :inserts (mapcar #'insert->plist (gloopy.pb::inserts st)))))

(defun add-synth-track (name &key (wave :saw) (attack 0.01) (decay 0.1)
                                  (sustain 0.8) (release 0.2) (gain 0.8))
  "Create a synth instrument track; returns its stable id."
  (gloopy.pb::id
   (%unary "AddSynthTrack"
           (mk 'gloopy.pb::add-synth-track-request
               :name name :wave (wave-int wave) :attack (s attack) :decay (s decay)
               :sustain (s sustain) :release (s release) :gain (s gain))
           'gloopy.pb::track-id)))

(defun add-sampler-track (name path &key (root-note 60))
  "Create a one-shot sampler track from a .wav on the server; returns its id."
  (gloopy.pb::id
   (%unary "AddSamplerTrack"
           (mk 'gloopy.pb::add-sampler-track-request
               :name name :path (namestring path) :root-note (round root-note))
           'gloopy.pb::track-id)))

(defun add-sfz-track (name path)
  "Load a native SFZ instrument from PATH on the server; returns its track id."
  (gloopy.pb::id
   (%unary "AddSfzTrack"
           (mk 'gloopy.pb::add-sfz-track-request :name name :path (namestring path))
           'gloopy.pb::track-id)))

(defun add-audio-track (name)
  "Create an empty audio track (holds audio clips); returns its id."
  (gloopy.pb::id
   (%unary "AddAudioTrack" (mk 'gloopy.pb::add-audio-track-request :name name)
           'gloopy.pb::track-id)))

(defun add-plugin-track (identifier)
  "Create an instrument track hosting the plugin with IDENTIFIER (see list-plugins)."
  (let ((id (gloopy.pb::id
             (%unary "AddPluginTrack"
                     (mk 'gloopy.pb::add-plugin-track-request :identifier identifier)
                     'gloopy.pb::track-id))))
    (if (minusp id) (error "Gloopy: no plugin for identifier ~s" identifier) id)))

(defun set-track-params (id &key volume pan mute solo name)
  "Set track params.  Only supplied keys are sent.  NOTE: because proto3 omits
default values on the wire, you can't set volume/pan to exactly 0.0, nor turn
mute/solo *off*, through this call — use the OSC lane for those."
  (let ((req (mk 'gloopy.pb::track-params :id (round id))))
    (when volume (setf (gloopy.pb::volume req) (s volume)))
    (when pan    (setf (gloopy.pb::pan req) (s pan)))
    (when mute   (setf (gloopy.pb::mute req) t))
    (when solo   (setf (gloopy.pb::solo req) t))
    (when name   (setf (gloopy.pb::name req) name))
    (%ack "SetTrackParams" req)))

(defun set-synth-param (id name value)
  "Tweak the built-in synth engine on track ID.  NAME is a string, one of:
wave osc2wave osc2detune oscmix sub attack decay sustain release gain
ftype cutoff reso fenvamt fattack fdecay fsustain frelease lfotarget lforate lfodepth."
  (%ack "SetSynthParam" (mk 'gloopy.pb::synth-param-set
                            :track-id (round id) :name (string name) :value (s value))))

(defun remove-track (id) (%ack "RemoveTrack" (mk 'gloopy.pb::track-id :id (round id))))

;;; --- clips ------------------------------------------------------------------
(defun note (pitch start length &optional (velocity 0.8))
  "Build a Note for add-clip: MIDI PITCH, START/LENGTH in beats, VELOCITY 0..1."
  (mk 'gloopy.pb::note :pitch (round pitch) :start-beat (d start)
      :length-beats (d length) :velocity (s velocity)))

(defun add-clip (track-id &key (start 0) (length 4) (content 0) (looped t) notes (name ""))
  "Add a MIDI clip of NOTES (from #'note) to TRACK-ID.  CONTENT 0 means = length.
Returns (:track-id :index)."
  (let ((r (%unary "AddClip"
                   (mk 'gloopy.pb::add-clip-request
                       :track-id (round track-id) :start-beat (d start)
                       :length-beats (d length) :content-len-beats (d content)
                       :looped looped :notes notes :name name)
                   'gloopy.pb::clip-id)))
    (list :track-id (gloopy.pb::track-id r) :index (gloopy.pb::index r))))

(defun add-audio-clip (track-id path &key (start 0) (gain 1.0))
  "Import a .wav (server-side PATH) as an audio clip on TRACK-ID at START beats."
  (let ((r (%unary "AddAudioClip"
                   (mk 'gloopy.pb::add-audio-clip-request
                       :track-id (round track-id) :start-beat (d start)
                       :path (namestring path) :gain (s gain))
                   'gloopy.pb::clip-id)))
    (list :track-id (gloopy.pb::track-id r) :index (gloopy.pb::index r))))

(defun remove-clip (track-id index)
  (%ack "RemoveClip" (mk 'gloopy.pb::clip-ref :track-id (round track-id) :index (round index))))

(defun move-clip (track-id index start &key to-track)
  "Move clip INDEX on TRACK-ID to START beats; :to-track moves it to another track."
  (let ((req (mk 'gloopy.pb::move-clip-request
                 :track-id (round track-id) :index (round index) :start-beat (d start))))
    (when to-track (setf (gloopy.pb::to-track-id req) (round to-track)))
    (%ack "MoveClip" req)))

;;; --- mixer / effects --------------------------------------------------------
(defun list-inserts ()
  "List of mixer-insert plists ([0] is the master bus)."
  (mapcar #'insert->plist
          (gloopy.pb::inserts (%unary "ListInserts" (mk 'gloopy.pb::empty) 'gloopy.pb::insert-list))))

(defun add-effect (insert type)
  "Add a built-in effect (:gain :filter :delay :reverb) to INSERT; returns its slot."
  (gloopy.pb::slot
   (%unary "AddEffect"
           (mk 'gloopy.pb::add-effect-request :insert (round insert) :type (fx-int type))
           'gloopy.pb::effect-ref)))

(defun add-plugin-effect (insert identifier)
  "Host a plugin effect (by IDENTIFIER, see list-plugins) on INSERT; returns its slot."
  (let ((slot (gloopy.pb::slot
               (%unary "AddPluginEffect"
                       (mk 'gloopy.pb::add-plugin-effect-request
                           :insert (round insert) :identifier identifier)
                       'gloopy.pb::effect-ref))))
    (if (minusp slot) (error "Gloopy: could not add plugin effect ~s" identifier) slot)))

(defun remove-effect (insert slot)
  (%ack "RemoveEffect" (mk 'gloopy.pb::effect-ref :insert (round insert) :slot (round slot))))

(defun set-effect-param (insert slot name value)
  (%ack "SetEffectParam"
        (mk 'gloopy.pb::effect-param-set :insert (round insert) :slot (round slot)
            :name name :value (s value))))

(defun set-effect-bypass (insert slot bypassed)
  (%ack "SetEffectBypass"
        (mk 'gloopy.pb::effect-bypass-set :insert (round insert) :slot (round slot)
            :bypassed (and bypassed t))))

(defun effect-params (insert slot)
  "List of param plists (:name :value :min :max) for an effect."
  (mapcar (lambda (p)
            (list :name (gloopy.pb::name p) :value (gloopy.pb::value p)
                  :min (gloopy.pb::min p) :max (gloopy.pb::max p)))
          (gloopy.pb::params
           (%unary "GetEffectParams"
                   (mk 'gloopy.pb::effect-ref :insert (round insert) :slot (round slot))
                   'gloopy.pb::param-list))))

;;; --- plugins ----------------------------------------------------------------
(defun list-plugins ()
  "List of installed plugins (:name :format :instrument :identifier)."
  (mapcar #'plugin->plist
          (gloopy.pb::plugins (%unary "ListPlugins" (mk 'gloopy.pb::empty) 'gloopy.pb::plugin-list))))

(defun scan-plugins (&optional force)
  "Rescan (or restore the cache) and return the plugin list."
  (mapcar #'plugin->plist
          (gloopy.pb::plugins
           (%unary "ScanPlugins" (mk 'gloopy.pb::scan-plugins-request :force (and force t))
                   'gloopy.pb::plugin-list))))

(defun open-plugin-editor (track-id)
  (%ack "OpenPluginEditor" (mk 'gloopy.pb::track-id :id (round track-id))))

;;; --- project / render -------------------------------------------------------
(defun new-project  ()     (%ack "NewProject" (mk 'gloopy.pb::empty)))
(defun load-project (path) (%ack "LoadProject" (mk 'gloopy.pb::file-path :path (namestring path))))
(defun save-project (path) (%ack "SaveProject" (mk 'gloopy.pb::file-path :path (namestring path))))
(defun save-composition (path) ; directory "composition as code" format
  (%ack "SaveComposition" (mk 'gloopy.pb::file-path :path (namestring path))))
(defun load-composition (path)
  (%ack "LoadComposition" (mk 'gloopy.pb::file-path :path (namestring path))))

(defun render (path &key (tail-seconds 2.0))
  "Bounce the whole song to a WAV at PATH (server-side).  Blocks until done."
  (%ack "RenderToFile"
        (mk 'gloopy.pb::render-request :path (namestring path) :tail-seconds (d tail-seconds))))

;;; --- events (server stream) -------------------------------------------------
(defun event->plist (ev)
  (case (gloopy.pb::kind-case ev)
    (:transport (let ((ts (gloopy.pb::transport ev)))
                  (list :kind :transport :playing (gloopy.pb::playing ts)
                        :bpm (gloopy.pb::bpm ts) :position-beats (gloopy.pb::position-beats ts))))
    (:meters    (let ((m (gloopy.pb::meters ev)))
                  (list :kind :meters :peak-l (gloopy.pb::peak-l m)
                        :peak-r (gloopy.pb::peak-r m))))
    (t (list :kind :unknown))))

(defun subscribe (&key (transport t) (meters t) (interval-ms 200) (seconds 3) on-event)
  "Stream playhead + meter events for ~SECONDS seconds.  Returns the collected
event plists; also calls ON-EVENT (if given) with each as it arrives."
  (unless *channel* (error "Gloopy: not connected."))
  (let ((stream (ag-grpc:call-server-stream *channel* (svc "Subscribe")
                  (mk 'gloopy.pb::subscribe-request
                      :transport (and transport t) :meters (and meters t)
                      :interval-ms (round interval-ms))
                  :response-type 'gloopy.pb::event))
        (events '())
        (budget (max 1 (ceiling (* 2 seconds (/ 1000 (max 1 interval-ms)))))))
    (unwind-protect
         (loop repeat budget
               for ev = (ag-grpc:stream-receive-message stream)
               while ev
               for pl = (event->plist ev)
               do (push pl events)
                  (when on-event (funcall on-event pl)))
      (ignore-errors (ag-grpc:stream-finish stream)))
    (nreverse events)))
