;;;
;;;Part of: Vicare Scheme
;;;Contents: char-sets library
;;;Date: Fri Jun 12, 2009
;;;
;;;Abstract
;;;
;;;
;;;
;;;Copyright (c) 2009-2010, 2012, 2015, 2016 Marco Maggi <marco.maggi-ipsu@poste.it>
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
(library (vicare containers char-sets (0 4 2015 6 12))
  (export

    ;; bounds
    char-set-lower-bound	char-set-upper-bound
    char-set-inner-upper-bound	char-set-inner-lower-bound

    ;; constructors
    char-set			char-set-copy
    char-set-add		char-set-add!
    (rename (char-set-add	char-set-adjoin)
	    (char-set-add!	char-set-adjoin!))
    char-set-delete

    ;; inspection
    char-set-size		char-set-domain-ref
    char-set-count
    char-set-write
    char-set-hash

    ;; predicates
    char-set?			char-set?/internals
    char-set-empty?		char-set-contains?
    char-set=?			char-set<?
    char-set-superset?		char-set-superset?/strict
    char-set-subset?		char-set-subset?/strict
    (rename (char-set-subset?	char-set<=?))

    ;; set operations
    char-set-intersection	char-set-union
    char-set-difference		char-set-xor
    char-set-complement		char-set-difference+intersection

    ;; iterations
    char-set-for-each		char-set-map
    char-set-every		char-set-any
    char-set-filter		char-set-fold

    ;; string operations
    string->char-set		char-set->string

    ;; list operations
    char-set->list		list->char-set

    ;; cursors
    (rename (cursor?		char-set-cursor?))
    char-set-cursor		char-set-ref
    char-set-cursor-next	end-of-char-set?

    ;; predefined
    char-set:empty		char-set:full

    char-set:ascii
    char-set:ascii/dec-digit	(rename (char-set:ascii/dec-digit char-set:ascii/digit))
    char-set:ascii/oct-digit	char-set:ascii/hex-digit
    char-set:ascii/lower-case	char-set:ascii/upper-case
    char-set:ascii/letter	char-set:ascii/letter+digit
    char-set:ascii/punctuation	char-set:ascii/symbol
    char-set:ascii/control	char-set:ascii/whitespace
    char-set:ascii/graphic	char-set:ascii/printable
    char-set:ascii/blank

    char-set:ascii/vowels		char-set:ascii/consonants
    char-set:ascii/vowels/lower-case	char-set:ascii/consonants/lower-case
    char-set:ascii/vowels/upper-case	char-set:ascii/consonants/upper-case
    )
  (import (vicare)
    (vicare system $chars))


;;;; helpers

(define (%last ell)
  ;;Return the last element in the list ELL.
  ;;
  (car (let last-pair ((x ell))
	 (if (pair? (cdr x))
	     (last-pair (cdr x))
	   x))))

(define (%append-reverse rev-head tail)
  ;;Reverse the list REV-HEAD and prepend it to the list TAIL.
  ;;
  (if (null? rev-head)
      tail
    (%append-reverse (cdr rev-head)
		     (cons (car rev-head) tail))))


;;;; characters as items in domains and ranges

(define-constant INCLUSIVE-LOWER-BOUND		0)
(define-constant EXCLUSIVE-INNER-UPPER-BOUND	#xD800)
(define-constant EXCLUSIVE-INNER-LOWER-BOUND	#xDFFF)
(define-constant INCLUSIVE-UPPER-BOUND		#x10FFFF)

(define-constant char-set-lower-bound		(integer->char INCLUSIVE-LOWER-BOUND))
(define-constant char-set-inner-upper-bound	(integer->char (- EXCLUSIVE-INNER-UPPER-BOUND 1)))
(define-constant char-set-inner-lower-bound	(integer->char (+ 1 EXCLUSIVE-INNER-LOWER-BOUND)))
(define-constant char-set-upper-bound		(integer->char INCLUSIVE-UPPER-BOUND))

(module CHARACTERS-AS-ITEMS
  (item?
   item=?
   item<?		#;item>?
   item<=?		item>=?
   item->integer	integer->item

   item-minus
   item-min		item-max
   item-next		item-prev)

  (define item?			char?)
  (define item=?		char=?)
  (define item<?		char<?)
  ;;(define item>?		char>?)
  (define item<=?		char<=?)
  (define item>=?		char>=?)
  (define item->integer		char->integer)
  (define integer->item		integer->char)

  (define (%number-in-item-range? x)
    (or (and (<= INCLUSIVE-LOWER-BOUND x)
	     (<  x EXCLUSIVE-INNER-UPPER-BOUND))
	(and (<  EXCLUSIVE-INNER-LOWER-BOUND x)
	     (<= x INCLUSIVE-UPPER-BOUND))))

  (define (item-minus a b)
    (+ 1 (- (item->integer a)
	    (item->integer b))))

  (define (item-min a b)
    (if (item<? a b) a b))

  (define (item-max a b)
    (if (item<? a b) b a))

  (define (item-next ch range)
    (let* ((x  (+ 1 (item->integer ch))))
      (and (%number-in-item-range? x)
	   (let ((ch ($fixnum->char x)))
	     (if range
		 (and (<= x (item->integer (cdr range)))
		      ch)
	       ch)))))

  (define (item-prev ch range)
    (let* ((x  (- (item->integer ch) 1)))
      (and (%number-in-item-range? x)
	   (let ((ch ($fixnum->char x)))
	     (if range
		 (and (<= (item->integer (car range)) x)
		      ch)
	       ch)))))

  #| end of module: CHARACTERS-AS-ITEMS |# )


;;;; ranges of items
;;
;;A range is a pair of items: the car being the inclusive leftmost, the cdr being the
;;inclusive rightmost.  Ranges must *never* be mutated.
;;

(module RANGES-OF-ITEMS
  (make-range
   range?			range-contains?
   range-length
   range=?			range<?			range<=?
   range-contiguous?		range-superset?		range-superset?/strict
   range-start<?		range-start<=?
   range-last<?			range-last<=?
   range-overlapping?		range-concatenate	range-intersection
   range-union			range-difference	range-for-each
   range-every			range-any		range-fold
   range->list)
  (import CHARACTERS-AS-ITEMS)

  (define* (make-range start last)
    ;;Build  and  return  a  new  range  of items.   START  must  be  a  item  object
    ;;representing the  first in the  range (inclusive); LAST  must be a  item object
    ;;representing the last in the range (inclusive).
    ;;
    (if (and (item? start)
	     (item? last)
	     (item<=? start last))
	(cons start last)
      (assertion-violation __who__ "invalid range limits" start last)))

  (define (range? obj)
    ;;Return #t if OBJ is a valid range of items, else return #f.
    ;;
    (and (pair? obj)
	 (and (item? (car obj))
	      (item? (cdr obj))
	      (item<=? (car obj) (cdr obj)))))

  (define (range-contains? range obj)
    ;;Return #t if OBJ is contained in RANGE, else return #f.
    ;;
    (and (item>=? obj (car range))
	 (item<=? obj (cdr range))))

  (define (range-length range)
    ;;Return an exact integer representing the number of items in the RANGE.
    ;;
    (+ 1 (- (item->integer (cdr range))
	    (item->integer (car range)))))

  (define (range=? range-a range-b)
    ;;Return #t if the arguments represent the same range; else return #f.
    ;;
    (or (eq? range-a range-b)
	(and (item=? (car range-a) (car range-b))
	     (item=? (cdr range-a) (cdr range-b)))))

  (define (range<? range-a range-b)
    ;;Return #t if all the items in RANGE-A  have code point strictly less than all the
    ;;items in RANGE-B; else return #f.
    ;;
    (item<? (cdr range-a) (car range-b)))

  (define (range<=? range-a range-b)
    ;;Return #t if all the items in RANGE-A have code point less than or equal to the
    ;;rightmost item in RANGE-B; else return #f.
    ;;
    (item<=? (cdr range-a) (cdr range-b)))

  (define (range-contiguous? range-a range-b)
    ;;Return #t if the  rightmost item in RANGE-A is one less  than the leftmost item
    ;;in RANGE-B; else return #f.
    ;;
    (or (= 2 (item-minus (car range-b) (cdr range-a)))
	(= 2 (item-minus (car range-a) (cdr range-b)))))

  (define (range-superset? range-a range-b)
    ;;Return true if RANGE-A is a superset of RANGE-B or is equal to RANGE-B: all the
    ;;items in RANGE-B are in RANGE-A; else return #f.
    ;;
    (item<=? (car range-a) (car range-b) (cdr range-b) (cdr range-a)))

  (define (range-superset?/strict range-a range-b)
    ;;Return true  if RANGE-A  is strictly a  superset of RANGE-B:  all the  items in
    ;;RANGE-B are  in RANGE-A,  and some items  of RANGE-A are  not in  RANGE-B; else
    ;;return #f.
    ;;
    (or (and (item<=? (car range-a) (car range-b))
	     (item<?  (cdr range-b) (cdr range-a)))
	(and (item<?  (car range-a) (car range-b))
	     (item<=? (cdr range-b) (cdr range-a)))))

  (define (range-start<? range-a range-b)
    ;;Return #t  if the leftmost item  in RANGE-A is  less than the leftmost  item in
    ;;RANGE-B:
    ;;
    ;;   |---------| range-a
    ;;       |---------| range-b
    ;;
    ;;else return #f.
    ;;
    (item<? (car range-a) (car range-b)))

  (define (range-start<=? range-a range-b)
    ;;Return #t if the leftmost item in RANGE-A is less than or equal to the leftmost
    ;;item in RANGE-B:
    ;;
    ;;   |---------| range-a
    ;;       |---------| range-b
    ;;
    ;;or:
    ;;
    ;;   |---------| range-a
    ;;   |-----------| range-b
    ;;
    ;;else return #f.
    ;;
    (item<=? (car range-a) (car range-b)))

  (define (range-last<? range-a range-b)
    ;;Return #t if  the rightmost item in  RANGE-A is less than the  leftmost item in
    ;;RANGE-B:
    ;;
    ;;   |---------| range-a
    ;;       |---------| range-b
    ;;
    ;;else return #f.
    ;;
    (item<? (cdr range-a) (cdr range-b)))

  (define (range-last<=? range-a range-b)
    ;;Return  #t if  the rightmost  item in  RANGE-A  is less  than or  equal to  the
    ;;leftmost item in RANGE-B:
    ;;
    ;;   |---------| range-a
    ;;       |---------| range-b
    ;;
    ;;or:
    ;;
    ;;   |-------------| range-a
    ;;     |-----------| range-b
    ;;
    ;;else return #f.
    ;;
    (item<=? (cdr range-a) (cdr range-b)))

  (define (range-overlapping? range-a range-b)
    ;;Return #t if the two ranges are overlapping:
    ;;
    ;;   |--------------| range-a
    ;;      |--------------| range-b
    ;;
    ;;or:
    ;;
    ;;       |--------------| range-a
    ;;   |--------------| range-b
    ;;
    ;;or:
    ;;
    ;;   |--------------| range-a
    ;;      |-------| range-b
    ;;
    ;;or:
    ;;
    ;;      |-------| range-a
    ;;   |--------------| range-b
    ;;
    ;;or:
    ;;
    ;;   |--------------| range-a
    ;;   |--------------| range-b
    ;;
    (let ((start-a (car range-a)) (last-a (cdr range-a))
	  (start-b (car range-b)) (last-b (cdr range-b)))
      (or (and (item<=? start-a start-b last-a))
	  (and (item<=? start-b start-a last-b)))))

  (define (range-concatenate range-a range-b)
    ;;Return a new  range having: as leftmost  item the leftmost item  in RANGE-A and
    ;;RANGE-B;  as rightmost  item  the  rightmost item  in  RANGE-A  and RANGE-B  a.
    ;;Example:
    ;;
    ;;    |--------| range-a
    ;;       |--------| range-b
    ;;    |-----------| result
    ;;
    ;;another example:
    ;;
    ;;    |--------| range-a
    ;;                   |--------| range-b
    ;;    |-----------------------| result
    ;;
    (cons (item-min (car range-a) (car range-b))
	  (item-max (cdr range-a) (cdr range-b))))

  (define (range-intersection range-a range-b)
    ;;If the  arguments have some  items in common:  return a new  range representing
    ;;their intersection, else return false.  Example:
    ;;
    ;;   |-------| range-a
    ;;      |-------| range-b
    ;;      |----| result
    ;;
    ;;another example:
    ;;
    ;;         |-------| range-a
    ;;   |-------| range-b
    ;;         |-| result
    ;;
    ;;for the following example the return value is false:
    ;;
    ;;   |-------| range-a
    ;;               |-------| range-b
    ;;
    (let ((start-a (car range-a)) (last-a (cdr range-a))
	  (start-b (car range-b)) (last-b (cdr range-b)))
      (and (or (item<=? start-a start-b last-a)
	       (item<=? start-b start-a last-b))
	   (cons (item-max start-a start-b)
		 (item-min last-a  last-b)))))

  (define (range-union range-a range-b)
    ;;Return two  values representing  the union  between the  given ranges:  all the
    ;;items that are in one or both the ranges.
    ;;
    ;;When the ranges do not overlap and  are not contiguous: the two returned values
    ;;are  both  ranges, the  first  being  the leftmost  and  the  second being  the
    ;;rightmost.
    ;;
    ;;When the ranges  do overlap or are  contiguous: the first returned  value is #f
    ;;and the  second returned  value is  a new range  representing the  actual union
    ;;between the arguments.
    ;;
    ;;Example:
    ;;
    ;;   |------| range-a
    ;;          |------| range-b
    ;;   |-------------| second result     first result = #f
    ;;
    ;;Example:
    ;;
    ;;          |------| range-a
    ;;   |------| range-b
    ;;   |-------------| second result     first result = #f
    ;;
    ;;Example:
    ;;
    ;;   |------| range-a
    ;;            |------| range-b
    ;;   |------| first result
    ;;            |------| second result
    ;;
    ;;Example:
    ;;
    ;;            |------| range-a
    ;;   |------| range-b
    ;;   |------| first result
    ;;            |------| second result
    ;;
    ;;For this function it is mandatory that: if one of the returned values if #f, it
    ;;must be the first one; this property is used in the domain functions below.
    ;;
    (let ((start-a (car range-a)) (last-a (cdr range-a))
	  (start-b (car range-b)) (last-b (cdr range-b)))
      (cond
       ;;Contiguous: RANGE-A < RANGE-B.
       ((= 2 (item-minus start-b last-a))	(values #f (cons start-a last-b)))

       ;;Contiguous: RANGE-B < RANGE-A.
       ((= 2 (item-minus start-a last-b))	(values #f (cons start-b last-a)))

       ;;Disjoint: RANGE-A < RANGE-B.
       ((item<? last-a start-b)			(values range-a range-b))

       ;;Disjoint: RANGE-B < RANGE-A.
       ((item<? last-b start-a)			(values range-b range-a))

       ;;Here we know they are overlapping.
       (else
	(values #f (cons (item-min start-a start-b)
			 (item-max last-a  last-b)))))))

  (define (range-difference range-a range-b)
    ;;Return two values representing the difference between the given ranges: all the
    ;;items that are in RANGE-A or in RANGE-B but not in both.
    ;;
    ;;When the ranges do not overlap and  are not contiguous: the two returned values
    ;;are  both  ranges, the  first  being  the leftmost  and  the  second being  the
    ;;rightmost.
    ;;
    ;;When the ranges do not overlap and  are contiguous: the first returned value is
    ;;#f and the second  returned value is a new range  representing the actual union
    ;;between the arguments.
    ;;
    ;;When the ranges  do overlap:
    ;;
    ;;Example:
    ;;
    ;;   |------| range-a
    ;;          |------| range-b
    ;;   |-------------| second result     first result = #f
    ;;
    ;;Example:
    ;;
    ;;          |------| range-a
    ;;   |------| range-b
    ;;   |-------------| second result     first result = #f
    ;;
    ;;Example:
    ;;
    ;;   |------| range-a
    ;;            |------| range-b
    ;;   |------| first result
    ;;            |------| second result
    ;;
    ;;Example:
    ;;
    ;;            |------| range-a
    ;;   |------| range-b
    ;;   |------| first result
    ;;            |------| second result
    ;;
    ;;Example:
    ;;
    ;;   |------| range-a
    ;;       |------| range-b
    ;;   |---| first result
    ;;          |---| second result
    ;;
    ;;Example:
    ;;
    ;;   |------| range-a
    ;;   |---------| range-b
    ;;          |--| second result           first result = #f
    ;;
    ;;Example:
    ;;
    ;;      |------| range-a
    ;;   |---------| range-b
    ;;   |--| second result           first result = #f
    ;;
    ;;For this function it is mandatory that: if one of the returned values if #f, it
    ;;must be the first one; this property is used in the domain functions below.
    ;;
    (let ((start-a (car range-a)) (last-a (cdr range-a))
	  (start-b (car range-b)) (last-b (cdr range-b)))
      (cond
       ;;Contiguous: RANGE-A < RANGE-B.
       ((= 2 (item-minus start-b last-a))	(values #f (cons start-a last-b)))

       ;;Contiguous: RANGE-B < RANGE-A.
       ((= 2 (item-minus start-a last-b))	(values #f (cons start-b last-a)))

       ;;Disjoint: RANGE-A < RANGE-B.
       ((item<? last-a start-b)			(values range-a range-b))

       ;;Disjoint: RANGE-B < RANGE-A.
       ((item<? last-b start-a)			(values range-b range-a))

       ;;Here we know they are overlapping.
       ((item=? start-a start-b) ; same start
	(cond ((item=? last-a last-b)
	       (values #f #f))
	      ((item<? last-a last-b)
	       (values #f (let ((last-a/next (item-next last-a range-b)))
			    (and (item<=? last-a/next last-b)
				 (cons last-a/next last-b)))))
	      ((item<? last-b last-a)
	       (values #f (let ((last-b/next (item-next last-b range-a)))
			    (and (item<=? last-b/next last-a)
				 (cons last-b/next last-a)))))
	      (else
	       (error #f "internal-error"))))

       ((item=? last-a last-b) ; same last
	(cond ((item=? start-a start-b)
	       (values #f #f))
	      ((item<? start-a start-b)
	       (values #f (let ((start-b/prev (item-prev start-b range-a)))
			    (and (item<=? start-a start-b/prev)
				 (cons start-a start-b/prev)))))
	      ((item<? start-b start-a)
	       (values #f (let ((start-a/prev (item-prev start-a range-b)))
			    (and (item<=? start-b start-a/prev)
				 (cons start-b start-a/prev)))))
	      (else
	       (error #f "internal-error"))))

       ;;Here we know that START-A != START-B and LAST-A != LAST-B.
       ((item<? start-a start-b) ; overlapping, a < b
	(values (let ((start-b/prev (item-prev start-b range-a)))
		  (and (item<=? start-a start-b/prev)
		       (cons start-a start-b/prev)))
		(if (item<=? last-a last-b)
		    (let ((last-a/next (item-next last-a range-b)))
		      (and (item<=? last-a/next last-b)
			   (cons last-a/next last-b)))
		  (let ((last-b/next (item-next last-b range-a)))
		    (and (item<=? last-b/next last-a)
			 (cons last-b/next last-a))))))

       (else	; overlapping, a > b
	(assert (item<? start-b start-a))
	(values (let ((start-a/prev (item-prev start-a range-b)))
		  (and (item<=? start-b start-a/prev)
		       (cons start-b start-a/prev)))
		(if (item<? last-a last-b)
		    (let ((last-a/next (item-next last-a range-b)))
		      (and (item<=? last-a/next last-b)
			   (cons last-a/next last-b)))
		  (let ((last-b/next (item-next last-b range-a)))
		    (and (item<=? last-b/next last-a)
			 (cons last-b/next last-a)))))))))

  (define (range-for-each proc range)
    ;;Apply PROC to each item in RANGE, discard the results.
    ;;
    (let loop ((i (car range)))
      (and i
	   (begin
	     (proc i)
	     (loop (item-next i range))))))

  (define (range-every proc range)
    ;;Apply PROC to every item in RANGE; return true if all the applications returned
    ;;true, else return #f.  Stop applying at the first false result.
    ;;
    (let loop ((i (car range)))
      (if i
	  (and (proc i)
	       (loop (item-next i range)))
	#t)))

  (define (range-any proc range)
    ;;Apply PROC to every  item in RANGE; stop applying at the  first true result and
    ;;return true.  If all the applications return false: return false.
    ;;
    (let loop ((i (car range)))
      (and i
	   (or (proc i)
	       (loop (item-next i range))))))

  (define (range-fold kons knil range)
    (let loop ((i    (car range))
	       (knil knil))
      (if i
	  (loop (item-next i range) (kons i knil))
	knil)))

  (define (range->list range)
    (range-fold cons '() range))

  #| end of module: RANGES-OF-ITEMS |# )


;;;; domains of items
;;
;;A domain is null or a list of ranges sorted from the leftmost to the rightmost.
;;

(module DOMAINS-OF-ITEMS
  ( ;;
   make-domain			make-empty-domain
   domain-copy			domain-add-item		domain-add-range
   domain-add
   domain?			domain-size		domain-empty?
   domain-contains?		domain=?		domain<?
   domain-superset?		domain-superset?/strict
   domain-intersection		domain-union		domain-difference
   domain-complement		domain-for-each		domain-map
   domain-every			domain-filter
   domain-any			domain-fold		domain->list)
  (import RANGES-OF-ITEMS CHARACTERS-AS-ITEMS)

  (define-syntax-rule (make-empty-domain)
    '())

  (define* (make-domain items/ranges)
    ;;Given a list of items and/or ranges return a new domain.
    ;;
    (fold-left domain-add
      (make-empty-domain) items/ranges))

  (define (domain-copy domain)
    ;;Return a new domain equal to DOMAIN but having a new list structure.
    ;;
    (if (pair? domain)
	(cons (domain-copy (car domain))
	      (domain-copy (cdr domain)))
      domain))

  (define* (domain-add domain obj)
    (cond ((item? obj)
	   (domain-add-item  domain obj))
	  ((range? obj)
	   (domain-add-range domain obj))
	  (else
	   (procedure-argument-violation __who__
	     "invalid element for domain, expected character or character range" obj))))

  (define (domain-add-item domain obj)
    ;;Return a new domain having the same elements of DOMAIN and containing also OBJ.
    ;;
    (domain-add-range domain (make-range obj obj)))

  (define (domain-add-range domain new-R)
    ;;Add a new  range NEW-R to the  DOMAIN.  Return a domain that  may share structure
    ;;with DOMAIN.
    ;;
    (if (domain-empty? domain)
	(list new-R)
      (let ((next-R (car domain)))
	(cond ((range=? new-R next-R)
	       ;;The new  range is equal  to the next  range; just return  the previous
	       ;;domain unchanged.
	       domain)

	      ((range-contiguous? new-R next-R)
	       ;;The new range is contiguous with  the next range.  Concatenate the new
	       ;;and the  next ranges,  then recurse  to see  if the  new range  can be
	       ;;composed with the further first range.
	       (domain-add-range (cdr domain) (range-concatenate new-R next-R)))

	      ((range<? new-R next-R)
	       ;;The  new range  is completely  to the  left of  the next  range.  Just
	       ;;prepend the new range to the domain.
	       (cons new-R domain))

	      ((range-overlapping? new-R next-R)
	       ;;The new range overlaps with the next range.  Join the new and the next
	       ;;ranges, then recurse to see if the  new range can be composed with the
	       ;;further first range.
	       (receive (first second)
		   (range-union new-R next-R)
		 (let ((new-domain (domain-add-range (cdr domain) second)))
		   (if first
		       (cons first new-domain)
		     new-domain))))

	      (else
	       ;;The new range is completely to the  right of the next range.  Keep the
	       ;;next range and recurse.
	       (cons next-R (domain-add-range (cdr domain) new-R)))))))

  (define (domain? domain)
    ;;Return #t if DOMAIN is a valid domain, else return #f.
    ;;
    (or (null? domain)
	(let ((range1  (car domain))
	      (domain1 (cdr domain)))
	  (and (range? range1)
	       (or (null? domain1)
		   (let ((range2 (car domain1)))
		     (and (range? range2)
			  (range<? range1 range2)
			  (domain? domain1))))))))

  (define (domain-size domain)
    ;;Return an exact integer representing the number of items in the DOMAIN.
    ;;
    (fold-right (lambda (range size)
		  (+ size (range-length range)))
      0 domain))

  (define domain-empty? null?)

  (define (domain-contains? domain item)
    ;;Return true if the DOMAIN contains the ITEM.
    ;;
    (exists (lambda (range)
	      (range-contains? range item))
      domain))

  (define (domain=? domain-a domain-b)
    ;;Return true if the given domains are equal, range by range.
    ;;
    (or (eq? domain-a domain-b)
	(cond ((null? domain-a)	(null? domain-b))
	      ((null? domain-b)	(null? domain-a))
	      (else
	       (and (range=? (car domain-a)
			     (car domain-b))
		    (domain=? (cdr domain-a)
			      (cdr domain-b)))))))

  (define (domain<? domain-a domain-b)
    ;;Return true if all the items in DOMAIN-A  are strictly less than all the items in
    ;;DOMAIN-B.
    ;;
    (and (not (null? domain-a))
	 (not (null? domain-b))
	 (range<? (%last domain-a) (car domain-b))))

;;; --------------------------------------------------------------------

  (define (domain-superset? domain-a domain-b)
    ;;Return #t  is DOMAIN-A  contains all the  items in DOMAIN-B,  in other  words: if
    ;;DOMAIN-A is equal to DOMAIN-B or a strict superset of DOMAIN-B.
    ;;
    ;;Recurse looking for RANGE-B in DOMAIN-A.
    ;;
    (or (domain-empty? domain-b)
	(and (not (domain-empty? domain-a))
	     (if (range-superset? (car domain-a)
				  (car domain-b))
		 (domain-superset? domain-a (cdr domain-b))
	       (domain-superset? (cdr domain-a) domain-b)))))

  (define (domain-superset?/strict domain-a domain-b)
    ;;Return #t  is DOMAIN-A  contains all the  items in DOMAIN-B  and some  items from
    ;;DOMAIN-A are not in DOMAIN-B, in other words: if DOMAIN-A is a strict superset of
    ;;DOMAIN-B.
    ;;
    ;;Recurse looking for RANGE-B in DOMAIN-A.
    ;;
    (let look-for-range-b-in-domain-a ((superset? #f)
				       (domain-a domain-a)
				       (domain-b domain-b))
      (if (domain-empty? domain-b)
	  superset?
	(and (not (domain-empty? domain-a))
	     (let ((range-a (car domain-a))
		   (range-b (car domain-b)))
	       (cond ((range<? range-a range-b)
		      (look-for-range-b-in-domain-a #t (cdr domain-a) domain-b))
		     ((range-superset?/strict range-a range-b)
		      (look-for-range-b-in-domain-a #t domain-a (cdr domain-b)))
		     ((range=? range-a range-b)
		      (look-for-range-b-in-domain-a superset? (cdr domain-a) (cdr domain-b)))
		     ((range-superset? range-a range-b)
		      (look-for-range-b-in-domain-a superset? domain-a (cdr domain-b)))
		     (else #f)))))))

  (define* (domain-intersection domain-a domain-b)
    (let loop ((result	'())
	       (domain-a	domain-a)
	       (domain-b	domain-b))
      (if (or (domain-empty? domain-a)
	      (domain-empty? domain-b))
	  (reverse result)
	(let ((range-a	(car domain-a))
	      (range-b	(car domain-b)))
	  (cond
	   ((range=? range-a range-b)
	    (loop (cons range-a result)
		  (cdr domain-a) (cdr domain-b)))
	   ((range-overlapping? range-a range-b)
	    (let ((result (cons (range-intersection range-a range-b) result)))
	      (if (range-last<? range-a range-b)
		  (loop result (cdr domain-a) domain-b)
		(loop result domain-a (cdr domain-b)))))
	   ((range<? range-a range-b)
	    (loop result (cdr domain-a) domain-b))
	   ((range<? range-b range-a)
	    (loop result domain-a (cdr domain-b)))
	   (else
	    (assertion-violation __who__
	      "internal error processing ranges" (list range-a range-b))))))))

  (define* (domain-union domain-a domain-b)
    (define (finish result domain)
      (if (null? result)
	  domain
	(let loop ((result result)
		   (domain domain))
	  (if (domain-empty? domain)
	      (reverse result)
	    (let ((range (car domain))
		  (top   (car result)))
	      (cond
	       ((or (range-overlapping? top range)
		    (range-contiguous?  top range))
		(let-values (((head tail) (range-union top range)))
		  (loop (%cons-head-tail head tail (cdr result)) (cdr domain))))
	       (else
		(loop (cons range result) (cdr domain)))))))))
    (let loop ((result '())
	       (domain-a domain-a)
	       (domain-b domain-b))
      (cond
       ((domain-empty? domain-a)
	(finish result domain-b))
       ((domain-empty? domain-b)
	(finish result domain-a))
       (else
	(let ((range-a (car domain-a))
	      (range-b (car domain-b)))
	  (cond
	   ((and (not (null? result)) (range-contiguous? (car result) range-a))
	    (loop (cons (range-concatenate (car result) range-a) (cdr result))
		  (cdr domain-a) domain-b))

	   ((and (not (null? result)) (range-contiguous? (car result) range-b))
	    (loop (cons (range-concatenate (car result) range-b) (cdr result))
		  domain-a (cdr domain-b)))

	   ((and (not (null? result)) (range=? (car result) range-a))
	    (loop result (cdr domain-a) domain-b))

	   ((and (not (null? result)) (range=? (car result) range-b))
	    (loop result domain-a (cdr domain-b)))

	   ((and (not (null? result)) (range-overlapping? (car result) range-a))
	    (let-values (((head tail) (range-union (car result) range-a)))
	      (loop (cons tail (cdr result)) (cdr domain-a) domain-b)))

	   ((and (not (null? result)) (range-overlapping? (car result) range-b))
	    (let-values (((head tail) (range-union (car result) range-b)))
	      (loop (cons tail (cdr result)) domain-a (cdr domain-b))))

	   ((range=? range-a range-b)
	    (loop (cons range-a result) (cdr domain-a) (cdr domain-b)))

	   ((range-contiguous? range-a range-b)
	    (loop (cons (range-concatenate range-a range-b) result) (cdr domain-a) (cdr domain-b)))

	   ((range-overlapping? range-a range-b)
	    (let-values (((head tail) (range-union range-a range-b)))
	      (loop (cons tail result) (cdr domain-a) (cdr domain-b))))

	   ((range<? range-a range-b)
	    (loop (cons range-a result) (cdr domain-a) domain-b))

	   ((range<? range-b range-a)
	    (loop (cons range-b result) domain-a (cdr domain-b)))

	   (else
	    (assertion-violation __who__
	      "internal error processing ranges" (list range-a range-b)))))))))

  (define* (domain-difference domain-a domain-b)
    (define (finish result domain)
      (if (null? result)
	  domain
	(let loop ((result result)
		   (domain domain))
	  (if (domain-empty? domain)
	      (reverse result)
	    (let ((range (car domain))
		  (top   (car result)))
	      (cond ((range-overlapping? top range)
		     (let-values (((head tail) (range-difference top range)))
		       (loop (%cons-head-tail head tail (cdr result))
			     (cdr domain))))
		    ((range-contiguous? top range)
		     (let-values (((head tail) (range-union top range)))
		       (loop (%cons-head-tail head tail (cdr result))
			     (cdr domain))))
		    (else
		     (loop (cons range result) (cdr domain)))))))))
    (let loop ((result '())
	       (domain-a domain-a)
	       (domain-b domain-b))
      (cond
       ((and (domain-empty? domain-a) (domain-empty? domain-b))
	(reverse result))
       ((domain-empty? domain-a)
	(finish result domain-b))
       ((domain-empty? domain-b)
	(finish result domain-a))
       (else
	(let ((range-a (car domain-a))
	      (range-b (car domain-b)))
	  (cond
	   ((and (not (null? result)) (range-contiguous? (car result) range-a))
	    (loop (cons (range-concatenate (car result) range-a) (cdr result))
		  (cdr domain-a) domain-b))

	   ((and (not (null? result)) (range-contiguous? (car result) range-b))
	    (loop (cons (range-concatenate (car result) range-b) (cdr result))
		  domain-a (cdr domain-b)))

	   ((and (not (null? result)) (range-overlapping? (car result) range-a))
	    (let-values (((head tail) (range-difference (car result) range-a)))
	      (loop (%cons-head-tail head tail (cdr result)) (cdr domain-a) domain-b)))

	   ((and (not (null? result)) (range-overlapping? (car result) range-b))
	    (let-values (((head tail) (range-difference (car result) range-b)))
	      (loop (%cons-head-tail head tail (cdr result)) domain-a (cdr domain-b))))

	   ((range=? range-a range-b)
	    (loop result (cdr domain-a) (cdr domain-b)))

	   ((range-contiguous? range-a range-b)
	    (loop (cons (range-concatenate range-a range-b) result) (cdr domain-a) (cdr domain-b)))

	   ((range-overlapping? range-a range-b)
	    (let-values (((head tail) (range-difference range-a range-b)))
	      (loop (%cons-head-tail head tail result) (cdr domain-a) (cdr domain-b))))

	   ((range<? range-a range-b)
	    (loop (cons range-a result) (cdr domain-a) domain-b))

	   ((range<? range-b range-a)
	    (loop (cons range-b result) domain-a (cdr domain-b)))

	   (else
	    (assertion-violation __who__
	      "internal error processing ranges" (list range-a range-b)))))))))

  (define* (domain-complement domain universe)
    ;;Return a new domain holding the items from UNIVERSE not present in DOMAIN.
    ;;
    (if (null? domain)
	universe
      (let loop ((result	'())
		 (universe	universe)
		 (domain	domain))
	(cond ((domain-empty? universe)
	       (reverse result))
	      ((domain-empty? domain)
	       (reverse (%append-reverse universe result)))
	      (else
	       (let ((universe.range (car universe))
		     (domain.range   (car domain)))
		 (cond ((range<? domain.range universe.range)
			;;Discard the domain range, go on with the same universe.
			(loop result universe (cdr domain)))

		       ((range<? universe.range domain.range)
			;;Accept as  result the universe  range, go on with  the same
			;;domain.
			(loop (cons universe.range result) (cdr universe) domain))

		       ((range=? universe.range domain.range)
			;;Discarb both the ranges.
			(loop result (cdr universe) (cdr domain)))

		       ((range-overlapping? universe.range domain.range)
			(receive (head tail)
			    (%range-in-first-only universe.range domain.range)
			  (if (range-last<? domain.range universe.range)
			      ;;The scenario is one among:
			      ;;
			      ;;       |---------| universe.range
			      ;;   |---------| domain.range
			      ;;
			      ;;   |---------------| universe.range
			      ;;      |---------| domain.range
			      ;;
			      ;;Here we know that TAIL is non-false: we still have to
			      ;;check  the  TAIL  against  the next  range  from  the
			      ;;domain.
			      (loop (if head (cons head result) result) ;result
				    (cons tail (cdr universe))		;universe
				    (cdr domain))			;domain
			    (let ((result (%cons-head-tail head tail result)))
			      (cond ((range-last<? universe.range domain.range)
				     ;;The scenario is one among:
				     ;;
				     ;;   |---------| universe.range
				     ;;      |---------| domain.range
				     ;;
				     ;;   |---------| universe.range
				     ;;   |-------------| domain.range
				     ;;
				     ;;we  still  have  to  check  the  domain  range
				     ;;against the next range from the universe.
				     (loop result (cdr universe) domain))
				    (else
				     ;;The scenario is one among:
				     ;;
				     ;;   |------------| universe.range
				     ;;      |---------| domain.range
				     ;;
				     ;;      |---------| universe.range
				     ;;   |------------| domain.range
				     ;;
				     (loop result (cdr universe) (cdr domain))))))))
		       (else
			(assertion-violation __who__
			  "internal error processing ranges" (list universe.range domain.range)))
		       )))))))

  (define (domain-for-each proc domain)
    (for-each (lambda (range)
		(range-for-each proc range))
      domain))

  (define (domain-map proc domain)
    (domain-fold (lambda (item knil)
		   (domain-add-item knil (proc item)))
		 (make-empty-domain)
		 domain))

  (define (domain-filter pred domain base-domain)
    (domain-fold (lambda (item knil)
		   (if (pred item)
		       (domain-add-item knil item)
		     knil))
		 base-domain
		 domain))

  (define (domain-every proc domain)
    (for-all (lambda (range)
	       (range-every proc range))
      domain))

  (define (domain-any proc domain)
    (exists (lambda (range)
	      (range-any proc range))
      domain))

  (define (domain-fold kons knil domain)
    (if (null? domain)
	knil
      (domain-fold kons (range-fold kons knil (car domain)) (cdr domain))))

  (define (domain->list domain)
    (reverse (apply append (map range->list domain))))

;;; --------------------------------------------------------------------

  (define (%range-in-first-only range-a range-b)
    ;;Return two values, each being a range  or false.  The returned values, head and
    ;;tail, represent the subset of RANGE-A, not including the characters in RANGE-B.
    ;;The returned range may share structure with the arguments.
    ;;
    (let ((start-a (car range-a)) (last-a (cdr range-a))
	  (start-b (car range-b)) (last-b (cdr range-b)))
      (if (or
	   ;;    range-b     range-a
	   ;; --|+++++++|---|+++++++|--
	   (item<? last-b start-a)
	   ;;    range-a     range-b
	   ;; --|+++++++|---|+++++++|--
	   (item<? last-a start-b))
	  ;;The ranges are disjoint (including  contiguous): return false as head and
	  ;;the whole RANGE-A as tail.
	  (values #f range-a)
	;;Here we know they are overlapping.
	(values
	 ;;Return false as head and a domain as tail when:
	 ;;
	 ;;        range-a
	 ;; ------|+++++++|---
	 ;; --|+++++++|+++|---
	 ;;    range-b tail
	 ;;
	 ;;Return a domain as head and false as tail when:
	 ;;
	 ;;    range-a
	 ;; --|+++++++|------
	 ;; --|++|+++++++|---
	 ;;  head range-b
	 ;;
	 ;;Return a domain as both head and tail when:
	 ;;
	 ;;       range-a
	 ;; --|++++++++++++++|---
	 ;; --|++|+++++++|+++|---
	 ;;  head range-b tail
	 ;;
	 (and (item<? start-a start-b)
	      (let ((start-b/prev (item-prev start-b range-a)))
		(if (item<? start-a start-b/prev)
		    (cons start-a start-b/prev)
		  ;;start-a == start-b/prev
		  (cons start-a start-a))))
	 (and (item<? last-b last-a)
	      (let ((last-b/next (item-next last-b range-a)))
		(if (item<? last-b/next last-a)
		    (cons last-b/next last-a)
		  ;;last-b/next == last-a
		  (cons last-a last-a))))))))

  (define (%cons-head-tail head tail result)
    ;;This  is an  internal helper  for  set operations.   Prepend HEAD  and TAIL  to
    ;;RESULT, but only if they are ranges; if they are false: do nothing.
    ;;
    (let ((result (if head (cons head result) result)))
      (if tail (cons tail result) result)))

  #| end of module: DOMAINS-OF-ITEMS |# )


(import DOMAINS-OF-ITEMS)

(define-record-type (:char-set :make-char-set char-set?)
  (nongenerative nausicaa:char-sets:char-set)
  (fields (mutable domain char-set-domain-ref char-set-domain-set!)
		;Null or  a list of  pairs, each  representing a range  of characters
		;left-inclusive and right-inclusive.
	  ))

(define (char-set . args)
  ;;Build and return a  new instance of :CHAR-SET.  ARGS can be  a list of characters
  ;;and or ranges.
  ;;
  (:make-char-set (make-domain args)))

(define* (char-set-copy {cs char-set?})
  ;;Return a new instance of :CHAR-SET containing a copy of the fields of CS.
  ;;
  (:make-char-set (domain-copy ($:char-set-domain cs))))

(define* (char-set-add {cs char-set?} . char/range*)
  ;;Return a new instance of :CHAR-SET containing a copy of the fields of CS with the
  ;;addition of OBJ, which can be a character or range.
  ;;
  ($char-set-add cs char/range*))

(define ($char-set-add cs char/range*)
  ($char-set-union cs (list (apply char-set char/range*))))

(define* (char-set-add! {cs char-set?} . char/range*)
  ;;Return CS itself after adding OBJ to it; OBJ can be a character or range.
  ;;
  ($:char-set-domain-set! cs ($:char-set-domain ($char-set-add cs char/range*)))
  cs)

(define* (char-set-delete {cs char-set?} . char/range*)
  ($char-set-difference cs (list (apply char-set char/range*))))

;;; --------------------------------------------------------------------

(define (char-set?/internals cs)
  ;;Return #t if CS is an instance of :CHAR-SET and its field contents are valid.
  ;;
  (and (char-set? cs)
       (domain? ($:char-set-domain cs))))

(define* (char-set-empty? {cs char-set?})
  (domain-empty? ($:char-set-domain cs)))

(define* (char-set-contains? {cs char-set?} {item char?})
  (domain-contains? ($:char-set-domain cs) item))

(module (char-set=?)

  (case-define* char-set=?
    (()
     #t)
    (({cs char-set?})
     #t)
    (({cs1 char-set?} {cs2 char-set?})
     ($char-set=?/two cs1 cs2))
    (({cs1 char-set?} {cs2 char-set?} {cs3 char-set?} . {cs* char-set?})
     (and ($char-set=?/two cs1 cs2)
	  ($char-set=?/two cs2 cs3)
	  (for-all (lambda (cs^)
		     ($char-set=?/two cs3 cs^))
	    cs*)))
    #| end of CASE-DEFINE* |# )

  (define ($char-set=?/two cs1 cs2)
    (domain=? ($:char-set-domain cs1)
	      ($:char-set-domain cs2)))

  #| end of module |# )

;;; --------------------------------------------------------------------

(module (char-set<?)

  (case-define* char-set<?
    (()
     #t)
    (({cs char-set?})
     #t)
    (({cs1 char-set?} {cs2 char-set?})
     ($char-set<?/two cs1 cs2))
    (({cs1 char-set?} {cs2 char-set?} {cs3 char-set?} . {cs* char-set?})
     (and ($char-set<?/two cs1 cs2)
	  ($char-set<?/two cs2 cs3)
	  (apply char-set<? cs3 cs*)))
    #| end of CASE-DEFINE* |# )

  (define ($char-set<?/two cs1 cs2)
    (domain<? ($:char-set-domain cs1)
	      ($:char-set-domain cs2)))

  #| end of module |# )

;;; --------------------------------------------------------------------

(module (char-set-superset?)

  (case-define* char-set-superset?
    (()
     #t)
    (({cs char-set?})
     #t)
    (({cs1 char-set?} {cs2 char-set?})
     ($char-set-superset?/two cs1 cs2))
    (({cs1 char-set?} {cs2 char-set?} {cs3 char-set?} . {cs* char-set?})
     (and ($char-set-superset?/two cs1 cs2)
	  ($char-set-superset?/two cs2 cs3)
	  (apply char-set-superset? cs3 cs*)))
    #| end of CASE-DEFINE* |# )

  (define ($char-set-superset?/two cs1 cs2)
    (domain-superset? ($:char-set-domain cs1)
		      ($:char-set-domain cs2)))

  #| end of module |# )

;;; --------------------------------------------------------------------

(module (char-set-subset?)

  (case-define* char-set-subset?
    (()
     #t)
    (({cs char-set?})
     #t)
    (({cs1 char-set?} {cs2 char-set?})
     ($char-set-subset?/two cs1 cs2))
    (({cs1 char-set?} {cs2 char-set?} {cs3 char-set?} . {cs* char-set?})
     (and ($char-set-subset?/two cs1 cs2)
	  ($char-set-subset?/two cs2 cs3)
	  (apply char-set-subset? cs3 cs*)))
    #| end of CASE-DEFINE* |# )

  (define ($char-set-subset?/two cs1 cs2)
    (domain-superset? ($:char-set-domain cs2)
		      ($:char-set-domain cs1)))

  #| end of module |# )

;;; --------------------------------------------------------------------

(module (char-set-superset?/strict)

  (case-define* char-set-superset?/strict
    (()
     #t)
    (({cs char-set?})
     #t)
    (({cs1 char-set?} {cs2 char-set?})
     ($char-set-superset?/strict/two cs1 cs2))
    (({cs1 char-set?} {cs2 char-set?} {cs3 char-set?} . {cs* char-set?})
     (and ($char-set-superset?/strict/two cs1 cs2)
	  ($char-set-superset?/strict/two cs2 cs3)
	  (apply char-set-superset?/strict cs3 cs*))))

  (define ($char-set-superset?/strict/two cs1 cs2)
    (domain-superset?/strict ($:char-set-domain cs1)
			     ($:char-set-domain cs2)))

  #| end of module |# )

;;; --------------------------------------------------------------------

(module (char-set-subset?/strict)

  (case-define* char-set-subset?/strict
    (()
     #t)
    (({cs char-set?})
     #t)
    (({cs1 char-set?} {cs2 char-set?})
     ($char-set-subset?/strict/two cs1 cs2))
    (({cs1 char-set?} {cs2 char-set?} {cs3 char-set?} . {cs* char-set?})
     (and ($char-set-subset?/strict/two cs1 cs2)
	  ($char-set-subset?/strict/two cs2 cs3)
	  (apply char-set-subset?/strict cs3 cs*))))

  (define ($char-set-subset?/strict/two cs1 cs2)
    (domain-superset?/strict ($:char-set-domain cs2)
			     ($:char-set-domain cs1)))

  #| end of module |# )

;;; --------------------------------------------------------------------

(case-define* char-set-intersection
  (()
   (char-set-copy char-set:full))
  (({cs char-set?} . {cs* char-set?})
   (:make-char-set (fold-left (lambda (domain-prev cs)
				(domain-intersection ($:char-set-domain cs) domain-prev))
		     ($:char-set-domain cs)
		     cs*))))

(case-define* char-set-union
  (()
   (char-set))
  (({cs char-set?} . {cs* char-set?})
   ($char-set-union cs cs*)))

(define ($char-set-union cs cs*)
  (:make-char-set (fold-left (lambda (domain-prev cs)
			       (domain-union ($:char-set-domain cs) domain-prev))
		    ($:char-set-domain cs)
		    cs*)))

(define* (char-set-difference {universe char-set?} . {cs* char-set?})
  ($char-set-difference universe cs*))

(define ($char-set-difference universe cs*)
  ;;Return a  new char-set holding the  characters from UNIVERSE not  included in any
  ;;char-set from CS*.
  ;;
  (:make-char-set (fold-left (lambda (universe-domain cs)
			       ;;DOMAIN-COMPLEMENT returns  a new domain  holding the
			       ;;items from  the second  argument not present  in the
			       ;;first argument.
			       (domain-complement ($:char-set-domain cs) universe-domain))
		    ($:char-set-domain universe)
		    cs*)))



(define* (char-set-difference+intersection {cs char-set?} . {cs* char-set?})
  (let* ((domain ($:char-set-domain cs))
	 (Q      (fold-left (lambda (P cs)
			      (let ((domain-prev.diff (car P))
				    (domain-prev.inte (cdr P))
				    (domain           ($:char-set-domain cs)))
				(cons (domain-complement   domain domain-prev.diff)
				      (domain-intersection domain domain-prev.inte))))
		   (cons domain domain)
		   cs*)))
    (values (:make-char-set (car Q))	;difference
	    (:make-char-set (cdr Q))))) ;intersection

(case-define* char-set-xor
  (()
   (char-set))
  (({cs char-set?} . {cs* char-set?})
   ;;Return  a new  char-set  holding the  characters  from CS  not  included in  any
   ;;char-set from CS*.
   ;;
   (:make-char-set (fold-left (lambda (domain-prev cs)
				(domain-difference ($:char-set-domain cs) domain-prev))
		     ($:char-set-domain cs)
		     cs*))))

(case-define* char-set-complement
  (({cs char-set?})
   (:make-char-set (domain-complement ($:char-set-domain cs) ($:char-set-domain char-set:full))))
  (({cs char-set?} {universe char-set?})
   (:make-char-set (domain-complement ($:char-set-domain cs) ($:char-set-domain universe))))
  #| end of CASE-DEFINE* |# )

;;; --------------------------------------------------------------------

(define* (char-set-map {proc procedure?} {cs char-set?})
  (:make-char-set (domain-map proc ($:char-set-domain cs))))

(define* (char-set-for-each {proc procedure?} {cs char-set?})
  (domain-for-each proc ($:char-set-domain cs))
  (void))

(case-define* char-set-filter
  (({pred procedure?} {cs char-set?})
   (:make-char-set (domain-filter pred ($:char-set-domain cs) (make-empty-domain))))
  (({pred procedure?} {cs char-set?} {base-cs char-set?})
   (:make-char-set (domain-filter pred ($:char-set-domain cs) ($:char-set-domain base-cs)))))

(define* (char-set-every {proc procedure?} {cs char-set?})
  (domain-every proc ($:char-set-domain cs)))

(define* (char-set-any {proc procedure?} {cs char-set?})
  (domain-any proc ($:char-set-domain cs)))

(define* (char-set-fold {kons procedure?} knil {cs char-set?})
  (domain-fold kons knil ($:char-set-domain cs)))

;;; --------------------------------------------------------------------
;;; list operations

(define* (char-set->list {cs char-set?})
  (domain->list ($:char-set-domain cs)))

(case-define* list->char-set
  (({ell list-of-chars?})
   (apply char-set ell))
  (({ell list-of-chars?} {base-cs char-set?})
   (char-set-union base-cs (apply char-set ell))))

;;; --------------------------------------------------------------------
;;; string operations

(define* (char-set->string {cs char-set?})
  (receive (port extract)
      (open-string-output-port)
    (char-set-for-each (lambda (ch)
			 (display ch port))
		       cs)
    (extract)))

(case-define* string->char-set
  (({str string?})
   (:make-char-set (make-domain (string->list str))))
  (({str string?} {base-cs char-set?})
   (char-set-union base-cs (:make-char-set (make-domain (string->list str))))))

;;; --------------------------------------------------------------------

(define* (char-set-size {cs char-set?})
  (domain-size ($:char-set-domain cs)))

(define* (char-set-count {pred procedure?} {cs char-set?})
  (domain-fold (lambda (ch knil)
		 (if (pred ch)
		     (add1 knil)
		   knil))
	       0 ($:char-set-domain cs)))

(case-define* char-set-write
  ((cs)
   (char-set-write cs (current-output-port)))
  (({cs char-set?} {port textual-output-port?})
   (display "(char-set " port)
   (for-each (lambda (range)
	       (display "'(#\\x" port)
	       (display (number->string (char->integer (car range)) 16) port)
	       (display " . #\\x" port)
	       (display (number->string (char->integer (cdr range)) 16) port)
	       (display ") " port))
     ($:char-set-domain cs))
   (display #\) port)))

;;; --------------------------------------------------------------------

(case-define* char-set-hash
  ((cs)
   (char-set-hash cs (greatest-fixnum)))
  (({cs char-set?} {bound non-negative-exact-integer?})
   (let ((R (fold-left (lambda (knil range)
			 (+ knil
			    ($char->fixnum (car range))
			    ($char->fixnum (cdr range))))
	      0
	      ($:char-set-domain cs))))
     (if (null? R)
	 0
       (mod R bound)))))


;;;; iteration

(define-record-type cursor
  (nongenerative nausicaa:char-sets:cursor)
  (fields (immutable cs)
		;An instance of CHAR-SET.
	  (immutable ranges)
		;Null or the  list of ranges still to be  visited.  The first element
		;is  the range  we  are  visiting.  When  all  the  ranges have  been
		;visited, or the  char-set is empty to begin with:  this field is set
		;to null.
	  (immutable next char-set-ref)
		;False or the next character to  return.  It must be inside the first
		;range from the list in the RANGES  field.  When this field is set to
		;false, the field RANGES is set to null.
	  ))

(define* (char-set-cursor {cs char-set?})
  ;;RANGES is null or a list of ranges.
  (let ((ranges ($:char-set-domain cs)))
    (make-cursor cs ranges (if (null? ranges)
				#f
			      (caar ranges)))))

(define* (char-set-cursor-next {cursor cursor?})
  (let ((cur ($cursor-next cursor)))
    (and cur
	 (let ((next (integer->char (+ 1 (char->integer cur)))))
	   (let loop ((ranges (cursor-ranges cursor)))
	     (if (char<=? next (cdar ranges))
		 (make-cursor (cursor-cs cursor) ranges next)
	       (let ((ranges (cdr ranges)))
		 (if (null? ranges)
		     (make-cursor (cursor-cs cursor) '() #f)
		   (make-cursor (cursor-cs cursor) ranges (caar ranges))))))))))

(define* (end-of-char-set? {cursor cursor?})
  (not ($cursor-next cursor)))


;;;; basic predefined char sets

(define char-set:empty (:make-char-set '()))

(define char-set:full
  (:make-char-set `((,char-set-lower-bound       . ,char-set-inner-upper-bound)
		    (,char-set-inner-lower-bound . ,char-set-upper-bound))))


;;;; ASCII predefined char sets

(define char-set:ascii
  ;;Notice  that ASCII  has numeric  codes in  the range  [0,  127]; the
  ;;numeric code 127 is included, and the number of codes is 128.
  (:make-char-set '((#\x0 . #\x127))))

(define char-set:ascii/dec-digit
  (:make-char-set '((#\0 . #\9))))

(define char-set:ascii/oct-digit
  (:make-char-set '((#\0 . #\7))))

(define char-set:ascii/hex-digit
  (:make-char-set '((#\0 . #\9)	;this must be the first
		    (#\A . #\F)	;this must be the second
		    (#\a . #\f) ;this must be the third
		    )))

(define char-set:ascii/lower-case
  (:make-char-set '((#\a . #\z))))

(define char-set:ascii/upper-case
  (:make-char-set '((#\A . #\Z))))

(define char-set:ascii/letter
  (char-set-union char-set:ascii/lower-case
		  char-set:ascii/upper-case))

(define char-set:ascii/letter+digit
  (char-set-union char-set:ascii/letter
		  char-set:ascii/dec-digit))

(define char-set:ascii/punctuation
  ;;Yes I have verified that all of these have numeric code in the range
  ;;[0, 127] (Marco Maggi, Tue Jun 23, 2009).
  (char-set #\! #\" #\# #\% #\& #\' #\( #\) #\* #\, #\- #\.
	    #\/ #\: #\; #\? #\@ #\[ #\\ #\] #\_ #\{ #\}))

(define char-set:ascii/symbol
  ;;Yes I have verified that all of these have numeric code in the range
  ;;[0, 127] (Marco Maggi, Tue Jun 23, 2009).
  (char-set #\$ #\+ #\< #\= #\> #\^ #\` #\| #\~))

(define char-set:ascii/control
  ;;Notice that control characters are the ones whose numeric code is in
  ;;the range [0, 31] plus 127; the number of control characters is 33.
  (char-set '(#\x0 . #\x31)
	    (integer->char 127)))

(define char-set:ascii/whitespace
  (char-set #\x0009	   ; HORIZONTAL TABULATION
	    #\x000A	   ; LINE FEED
	    #\x000B	   ; VERTICAL TABULATION
	    #\x000C	   ; FORM FEED
	    #\x000D	   ; CARRIAGE RETURN
	    #\x0020))	   ; SPACE

(define char-set:ascii/blank
  (char-set #\tab #\space))

(define char-set:ascii/graphic
  (char-set-union char-set:ascii/letter+digit
		  char-set:ascii/punctuation
		  char-set:ascii/symbol))

(define char-set:ascii/printable
  (char-set-union char-set:ascii/whitespace
		  char-set:ascii/graphic)) ; NO-BREAK SPACE

(define char-set:ascii/vowels
  (char-set #\a #\e #\i #\o #\u
	    #\A #\E #\I #\O #\U))

(define char-set:ascii/vowels/lower-case
  (char-set #\a #\e #\i #\o #\u))

(define char-set:ascii/vowels/upper-case
  (char-set #\A #\E #\I #\O #\U))

(define char-set:ascii/consonants
  (char-set-complement char-set:ascii/vowels
		       char-set:ascii/letter))

(define char-set:ascii/consonants/lower-case
  (char-set-complement char-set:ascii/vowels/lower-case
		       char-set:ascii/lower-case))

(define char-set:ascii/consonants/upper-case
  (char-set-complement char-set:ascii/vowels/upper-case
		       char-set:ascii/upper-case))


;;;; done

#| end of library |# )

;;; end of file
