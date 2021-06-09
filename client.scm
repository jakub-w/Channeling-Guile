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
	     (msgpack)
	     (ice-9 pretty-print))


(define client (make-client "password"))

(client-connect client "ipc:///tmp/zeromq-server")
(format #t "~a\n"
	(if (client-start client)
	    "Client started"
	    "Client didn't start"))

(define (make-request obj)
  (client-request client (pack obj)))

;; (pretty-print (packing-table))
;; (format #t "~a\n" )
(format #t "~s\n" (make-request 'foo))

;; FIXME: This shouldn't be necessary but without it errors are reported on
;;        poll: Context was terminated.
;;        Also regardless of the inclusion of this line the client hangs
;;        indefinitely. Probably because the sockets don't close before the
;;        context is destroyed. And that's because the destructors for the
;;        handshaker and the Client aren't getting called.
;;        And if finalizers were to be called manually they won't destroy the
;;        Client because it's still guarded by the gc.
;;        But why did it work before? Because context wasn't static. It was
;;        protected only by the shared_ptr and got destroyed along with the
;;        rest of the Client.
;;        If the the destructors were called the context would go away,
;;        doesn't matter if it's static or not.
;;        I could just set the client to #f on client-close and run
;;        finalizers.
;;        Still. Why did it work before without ANY issues? Surely the
;;        destruction of objects wasn't ensured.
;;        One could argue it didn't work. Non-static context just wasn't
;;        getting destroyed, so it didn't wait for sockets to close.
;;        So what's the solution? Destroying the client and the server in
;;        {client,server}-close functions and checking if they're running on
;;        every method? Probably. (I.e. deleting them and setting the pointer
;;        in a foreign object to nullptr, and validating the pointer on every
;;        call).
;;        It would be probably best to accept that guile is more C-like and
;;        automatic destruction is not supported for foreign objects.
;;        Make a module that will load this library and add some utils to it.
;;        Utils like macros "with-client" and "with-server" that will close
;;        the client and the server on exit (with dynamic-wind).
;;        Maybe even not expose make-{client,server}, just the macros.
;; NOTE:  Remember that finalizers aren't thread-safe so one could be called
;;        while we're calling client-stop for example (Not likely, but still).
(client-stop client)
