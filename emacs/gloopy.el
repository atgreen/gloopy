;;; gloopy.el --- Connect Emacs (Sly) to a running Gloopy kernel -*- lexical-binding: t; -*-

;; SPDX-FileCopyrightText: 2026 Anthony Green <green@moxielogic.com>
;; SPDX-License-Identifier: AGPL-3.0-only

;; Author: Anthony Green <green@moxielogic.com>
;; URL: https://github.com/atgreen/gloopy
;; Version: 0.1.0
;; Package-Requires: ((emacs "27.1") (sly "1.0"))
;; Keywords: multimedia, lisp, tools

;;; Commentary:

;; Gloopy runs a resident SBCL "kernel" that generates the notes for script
;; clips.  That kernel also hosts a Slynk server, so you can attach Sly/SLIME to
;; the very image that produces your music and redefine generators live.
;;
;; `M-x gloopy-connect' reads the discovery file Gloopy writes on startup
;; (XDG_RUNTIME_DIR/gloopy/kernel.json, else ~/.cache/gloopy/kernel.json),
;; connects Sly to the Slynk port it advertises, and enables `gloopy-mode'.
;; While that mode is on, Emacs advertises itself back to Gloopy (via an Emacs
;; server) so that clicking a script clip in the Gloopy GUI opens its source
;; buffer here, in the editor you're already working in.
;;
;; Setup:
;;   (add-to-list 'load-path "/path/to/gloopy/emacs")
;;   (require 'gloopy)
;; then, with Gloopy running:  M-x gloopy-connect

;;; Code:

(require 'json)
(require 'server)

(defgroup gloopy nil
  "Connect Emacs to a running Gloopy kernel."
  :group 'external
  :prefix "gloopy-")

(defcustom gloopy-server-name "gloopy"
  "Name of the Emacs server Gloopy uses to open script clips here.
`gloopy-connect' starts a server under this name if one is not already
running, so Gloopy can reach this Emacs without disturbing any default
server you may already use."
  :type 'string
  :group 'gloopy)

(defcustom gloopy-repl-package "GLOOPY-KERNEL"
  "Package a fresh Gloopy REPL switches to on connect.
This is the package a clip's generator is loaded into, so you land where
your generators live instead of in CL-USER."
  :type 'string
  :group 'gloopy)

(defcustom gloopy-replace-banner t
  "When non-nil, replace SLY's startup banner with a Gloopy one in Gloopy REPLs."
  :type 'boolean
  :group 'gloopy)

(defconst gloopy--repl-banner
  (concat
   ";; ================= Gloopy kernel =================\n"
   ";;  You're attached to the live image that generates\n"
   ";;  your clips. Redefine a generator and re-run the\n"
   ";;  clip — no restart. You start in the project package.\n"
   ";; ================================================\n\n")
  "Banner inserted at the top of a fresh Gloopy REPL.")

(defvar gloopy--connection nil
  "The Sly connection `gloopy-connect' opened, so REPL setup only touches ours.")

(defun gloopy--runtime-dir ()
  "Directory Gloopy shares its discovery/presence files in."
  (let ((xdg (getenv "XDG_RUNTIME_DIR")))
    (expand-file-name "gloopy"
                      (if (and xdg (file-directory-p xdg))
                          xdg
                        (expand-file-name ".cache" (or (getenv "HOME") "~"))))))

(defun gloopy--kernel-file ()
  "Path to the kernel discovery file Gloopy writes on startup."
  (expand-file-name "kernel.json" (gloopy--runtime-dir)))

(defun gloopy--emacs-file ()
  "Path to the presence file this package writes for Gloopy to find us."
  (expand-file-name "emacs.json" (gloopy--runtime-dir)))

(defun gloopy--read-json (file)
  "Read FILE as a JSON object into an alist, or nil if it is missing/unreadable."
  (when (file-readable-p file)
    (ignore-errors
      (let ((json-object-type 'alist)
            (json-array-type 'list)
            (json-key-type 'symbol))
        (json-read-file file)))))

;;;###autoload
(defun gloopy-connect ()
  "Connect Sly to the running Gloopy kernel and enable `gloopy-mode'.
Reads the Slynk port from Gloopy's discovery file and attaches Sly to it.
Signals a clear error if Gloopy is not running or Sly is unavailable."
  (interactive)
  (unless (require 'sly nil t)
    (user-error "gloopy-connect needs Sly installed (M-x package-install RET sly)"))
  (let* ((file (gloopy--kernel-file))
         (info (gloopy--read-json file)))
    (unless info
      (user-error "No Gloopy kernel found (%s).  Is Gloopy running with SBCL installed?"
                  file))
    (let ((port (alist-get 'slynk_port info)))
      (unless (and (integerp port) (> port 0))
        (user-error "Gloopy kernel has no Slynk port yet; wait for the kernel to warm up"))
      (setq gloopy--connection (funcall (intern "sly-connect") "127.0.0.1" port))
      (gloopy-mode 1)
      (message "gloopy: connected to kernel Slynk on 127.0.0.1:%d" port))))

(defun gloopy--setup-repl ()
  "Set up a fresh Gloopy MREPL: Gloopy banner, and start in `gloopy-repl-package'.
Run from `sly-mrepl-hook'.  Only touches the REPL of the connection
`gloopy-connect' opened, so it never disturbs other Sly REPLs."
  (when (and gloopy--connection
             (fboundp 'sly-current-connection)
             (eq (sly-current-connection) gloopy--connection))
    (when gloopy-replace-banner
      (let ((inhibit-read-only t))
        (save-excursion
          (goto-char (point-min))
          ;; Drop SLY's sylvester banner (everything above its "; SLY" note), if present.
          (if (re-search-forward "^;+ +SLY " nil t)
              (delete-region (point-min) (line-beginning-position))
            (goto-char (point-min)))
          (goto-char (point-min))
          (insert gloopy--repl-banner))))
    (when (fboundp 'sly-mrepl--eval-for-repl)
      (ignore-errors
        (sly-mrepl--eval-for-repl
         `(slynk-mrepl:guess-and-set-package ,gloopy-repl-package))))))

(add-hook 'sly-mrepl-hook #'gloopy--setup-repl)

(defun gloopy-disconnect ()
  "Turn off `gloopy-mode' (stop advertising this Emacs to Gloopy).
This does not disconnect Sly; use `sly-disconnect' for that."
  (interactive)
  (gloopy-mode -1))

;;;###autoload
(defun gloopy-open-clip (path)
  "Open the script clip source at PATH and bring this Emacs to the front.
Gloopy calls this over `emacsclient' when you click a script clip in the GUI."
  (let ((buf (find-file-noselect path)))
    (pop-to-buffer buf)
    (let ((frame (window-frame (get-buffer-window buf))))
      (when (frame-live-p frame)
        (raise-frame frame)
        (select-frame-set-input-focus frame))))
  path)

(defun gloopy--server-socket ()
  "Absolute path to this Emacs server's socket, or nil if not determinable."
  (let ((dir (or server-socket-dir
                 (and (getenv "XDG_RUNTIME_DIR")
                      (expand-file-name "emacs" (getenv "XDG_RUNTIME_DIR"))))))
    (when dir (expand-file-name server-name dir))))

(defun gloopy--write-presence ()
  "Tell Gloopy that this Emacs is available, and how to reach it."
  (make-directory (gloopy--runtime-dir) t)
  (let* ((socket (gloopy--server-socket))
         (obj `((server_name . ,server-name)
                (server_socket . ,(or socket ""))
                (pid . ,(emacs-pid)))))
    (with-temp-file (gloopy--emacs-file)
      (insert (json-encode obj)))))

(defun gloopy--clear-presence ()
  "Remove this Emacs's presence file if it still points at us."
  (let ((file (gloopy--emacs-file)))
    (when (file-exists-p file)
      (let ((info (gloopy--read-json file)))
        (when (or (null info)
                  (equal (alist-get 'pid info) (emacs-pid)))
          (ignore-errors (delete-file file)))))))

;;;###autoload
(define-minor-mode gloopy-mode
  "Global mode that links this Emacs to a running Gloopy.
When on, Emacs runs a server named `gloopy-server-name' and writes a
presence file so Gloopy can open script-clip buffers here."
  :global t
  :group 'gloopy
  :lighter " Gloopy"
  (if gloopy-mode
      (progn
        ;; We need an Emacs server so Gloopy can reach us.  If this Emacs already
        ;; runs one (a daemon, a user `server-start'), keep it and just record
        ;; its name; otherwise start one under our own name.
        (unless (server-running-p)
          (setq server-name gloopy-server-name)
          (server-start))
        (gloopy--write-presence)
        (add-hook 'kill-emacs-hook #'gloopy--clear-presence))
    (gloopy--clear-presence)
    (remove-hook 'kill-emacs-hook #'gloopy--clear-presence)))

(provide 'gloopy)
;;; gloopy.el ends here
