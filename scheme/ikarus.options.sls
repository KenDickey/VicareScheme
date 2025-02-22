;;; -*- coding: utf-8-unix -*-
;;;
;;;Part of: Vicare Scheme
;;;Contents: configuration options
;;;Date: Mon Jun  4, 2012
;;;
;;;Abstract
;;;
;;;
;;;
;;;Copyright (C) 2012-2016 Marco Maggi <marco.maggi-ipsu@poste.it>
;;;
;;;This program is free software: you can  redistribute it and/or modify it under the
;;;terms  of  the GNU  General  Public  License as  published  by  the Free  Software
;;;Foundation,  either version  3  of the  License,  or (at  your  option) any  later
;;;version.
;;;
;;;This program is  distributed in the hope  that it will be useful,  but WITHOUT ANY
;;;WARRANTY; without  even the implied warranty  of MERCHANTABILITY or FITNESS  FOR A
;;;PARTICULAR PURPOSE.  See the GNU General Public License for more details.
;;;
;;;You should have received a copy of  the GNU General Public License along with this
;;;program.  If not, see <http://www.gnu.org/licenses/>.
;;;


#!vicare
(library (ikarus.options)
  (options typed-language)
  (export
    print-verbose-messages?
    print-debug-messages?
    print-library-debug-messages?
    print-loaded-libraries?

    debug-mode-enabled?
    drop-assertions?
    writing-boot-image?
    strict-r6rs

    ;; vicare configuration options
    vicare-built-with-arguments-validation-enabled
    vicare-built-with-srfi-enabled
    vicare-built-with-ffi-enabled
    vicare-built-with-iconv-enabled
    vicare-built-with-posix-enabled
    vicare-built-with-glibc-enabled
    vicare-built-with-linux-enabled

    ;; conditional expansion
    cond-boot-expansion
    inclusion-in-normal-boot-image
    inclusion-in-rotation-boot-image
    bootstrapping-for-normal-boot-image
    bootstrapping-for-rotation-boot-image)
  (import (vicare))

  (include "cond-boot-expansion.scm" #t)


;;;; some boolean options

(define-syntax define-boolean-option
  (syntax-rules ()
    ((_ ?who)
     (define-boolean-option ?who #f))
    ((_ ?who ?default)
     (define/typed {?who (case-lambda
			   (()		=> (<boolean>))
			   ((<top>)	=> ()))}
       (let (({bool <boolean>} ?default))
	 (case-lambda
	   (()
	    bool)
	   ((value)
	    (set! bool (and value #t)))))))
    ))

(define-boolean-option debug-mode-enabled?)
(define-boolean-option print-verbose-messages?)
(define-boolean-option print-debug-messages?)
(define-boolean-option print-library-debug-messages?)
(define-boolean-option print-loaded-libraries?)

;;Set  to true  when  the fasl  writer  is writing  output for  a  boot image;  false
;;otherwise
;;
(define-boolean-option writing-boot-image?)


;;;; some parameter boolean options

(define-syntax define-parameter-boolean-option
  (syntax-rules ()
    ((_ ?who)
     (define-parameter-boolean-option ?who #f))
    ((_ ?who ?default)
     (define/typed {?who <parameter-procedure>}
       (make-parameter ?default
	 (lambda (value)
	   (and value #t)))))
    ))

(define-parameter-boolean-option strict-r6rs #f)

;;When  set to  true: expand  every ASSERT  macro into  its expression,  dropping the
;;assertions.  Specifically:
;;
;;   (assert ?expr)
;;
;;is expanded into:
;;
;;   ?expr
;;
;;so that side effects in ?EXPR are performed and the resulting value is returned.
(define-parameter-boolean-option drop-assertions?)


;;;; vicare build configuration options

(module (vicare-built-with-arguments-validation-enabled
	 vicare-built-with-srfi-enabled
	 vicare-built-with-ffi-enabled
	 vicare-built-with-iconv-enabled
	 vicare-built-with-posix-enabled
	 vicare-built-with-glibc-enabled
	 vicare-built-with-linux-enabled)
  (module (arguments-validation
	   VICARE_BUILT_WITH_SRFI_ENABLED
	   VICARE_BUILT_WITH_ICONV_ENABLED
	   VICARE_BUILT_WITH_FFI_ENABLED
	   VICARE_BUILT_WITH_POSIX_ENABLED
	   VICARE_BUILT_WITH_GLIBC_ENABLED
	   VICARE_BUILT_WITH_LINUX_ENABLED)
    (include "ikarus.config.scm" #t))
  (define ({vicare-built-with-arguments-validation-enabled <boolean>})
    arguments-validation)
  (define ({vicare-built-with-srfi-enabled  <boolean>})	VICARE_BUILT_WITH_SRFI_ENABLED)
  (define ({vicare-built-with-iconv-enabled <boolean>})	VICARE_BUILT_WITH_ICONV_ENABLED)
  (define ({vicare-built-with-ffi-enabled   <boolean>})	VICARE_BUILT_WITH_FFI_ENABLED)
  (define ({vicare-built-with-posix-enabled <boolean>})	VICARE_BUILT_WITH_POSIX_ENABLED)
  (define ({vicare-built-with-glibc-enabled <boolean>})	VICARE_BUILT_WITH_GLIBC_ENABLED)
  (define ({vicare-built-with-linux-enabled <boolean>})	VICARE_BUILT_WITH_LINUX_ENABLED)
  #| end of module |# )


;;;; done

;; #!vicare
;; (define dummy
;;   (foreign-call "ikrt_print_emergency" #ve(ascii "ikarus.options")))

#| end of library |# )

;;; end of file
