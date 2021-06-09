(define WORKING-DIR (dirname (current-filename)))

(define (append-env name value)
  (let ((old-val (getenv name)))
    (if old-val
	(setenv name (string-append old-val ":" value))
	(setenv name value))))

(append-env "LD_LIBRARY_PATH" WORKING-DIR)
(add-to-load-path (string-append WORKING-DIR "/include/guile-msgpack/"))

(load-extension "libchanneling-guile" "init_channeling_wrapper")

(use-modules (rnrs bytevectors)
	     (ice-9 threads)
	     (msgpack))


(define (error-handler key . args)
  (if (>= 4 (length args))
      (let ((subr (car args))
	    (message (cadr args))
	    (message-args (caddr args))
	    (rest (cdddr args)))
	(format (current-error-port)
		"In procedure ~a: ~a: '~a' ~a\n"
		subr key
		(apply format #f message message-args)
		rest))
      (format (current-error-port) "~a: ~s\n" key args))
  #f)

(define response "tralala wysyłam inną wiadomość")
;; (define response "this is a string without any utf-8")
(define (message-handler bytes)
  (let* ((msg (unpack bytes))
	 (response (false-if-exception
		    (pack (list->vector
			   (cons "lalala" (vector->list msg)))))))
    (format #t "~a\n" msg)
    (if response
	response
	(make-bytevector 0))))


(define server (make-server "password" message-handler))

(sigaction SIGINT
  (lambda (_)
    (server-close server)))

(server-bind server "ipc:///tmp/zeromq-server")

(format #t "Starting server...\n")

(server-run server)

(format #t "Server stopped\n")

;; (numerator 1.42234234)
;; (* (rationalize (inexact->exact 1.42234234) 1/10000000) 1.0)
;; (denominator 1.42234234)
