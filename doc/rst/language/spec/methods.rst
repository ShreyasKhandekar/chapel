.. default-domain:: chpl

.. index::
   single: methods
   single: methods; primary
   single: methods; secondary
   single: primary methods
   single: secondary methods
.. _Chapter-Methods:

=======
Methods
=======

A *method* is a procedure or iterator that is associated with an
expression known as the *receiver*.

Methods are declared with the following syntax: 

.. code-block:: syntax

   method-declaration-statement:
     procedure-kind[OPT] proc-or-iter this-intent[OPT] type-binding[OPT] identifier argument-list[OPT]
       return-intent[OPT] return-type[OPT] where-clause[OPT] function-body
     procedure-kind[OPT] 'operator' type-binding[OPT] identifier argument-list[OPT]
       return-intent[OPT] return-type[OPT] where-clause[OPT] function-body

   proc-or-iter:
     'proc'
     'iter'

   this-intent:
     'param'
     'type'
     'ref'
     'const ref'
     'const'

   type-binding:
     identifier .
     '(' expression ')' .

Methods defined within the lexical scope of a class, record, or union
are referred to as *primary methods*. For such methods, the
``type-binding`` is omitted and is taken to be the innermost class,
record, or union in which the method is defined.

Methods defined outside of the lexical scope of a class, record, or union,
but within the scope where the type itself is defined are known as
*secondary methods*.  Methods defined outside of both the lexical scope and
the scope where the type itself is defined are known as *tertiary methods*.
Both secondary and tertiary methods must have a ``type-binding`` (otherwise,
they would simply be standalone functions rather than methods). Note that
secondary and tertiary methods can be defined not only for classes, records,
and unions, but also for any other type (e.g., integers, reals, strings).

.. _Secondary_And_Tertiary_Methods_with_Type_Expressions:

Secondary and tertiary methods can be declared with a type expression instead of
a type identifier. In particular, if the ``type-binding`` is a parenthesized
expression, the compiler will evaluate that expression to find the receiver type
for the method. In that case, the method applies only to receivers of that type.
See also :ref:`Creating_General_and_Specialized_Versions_of_a_Function`.

Method calls are described in :ref:`Method_Calls`.

The use of ``this-intent`` is described in :ref:`Method_receiver_and_this`.

.. index::
   single: method calls
   single: methods; calling
   single: calls; methods
.. _Method_Calls:

Method Calls
------------

A method is invoked with a method call, which is similar to a non-method
call expression, but it can include a receiver clause. The receiver
clause syntactically identifies a single argument by putting it before
the method name. That argument is the method receiver. When calling a
method from another method, or from within a class or record
declaration, the receiver clause can be omitted. See
:ref:`Implicit_this_dot_in_methods`.



.. code-block:: syntax

   method-call-expression:
     receiver-clause[OPT] expression ( named-expression-list )
     receiver-clause[OPT] expression [ named-expression-list ]
     receiver-clause[OPT] parenthesesless-function-identifier

The receiver-clause (or its absence) specifies the method’s receiver
:ref:`Method_receiver_and_this`.

   *Example (defineMethod.chpl)*.

   A method to output information about an instance of the ``Actor``
   class can be defined as follows: 

   .. BLOCK-test-chapelpre

      class Actor {
        var name: string;
        var age: uint;
      }
      var anActor = new Actor(name="Tommy", age=27);
      writeln(anActor);

   

   .. code-block:: chapel

      proc Actor.print() {
        writeln("Actor ", name, " is ", age, " years old");
      }

   

   .. BLOCK-test-chapelpost

      anActor.print();

   

   .. BLOCK-test-chapeloutput

      {name = Tommy, age = 27}
      Actor Tommy is 27 years old

   This method can be called on an instance of the ``Actor`` class,
   ``anActor``, with the call expression ``anActor.print()``.

The actual arguments supplied in the method call are bound to the formal
arguments in the method declaration following the rules specified for
procedures (:ref:`Chapter-Procedures`). The exception is the
receiver :ref:`Method_receiver_and_this`.

A primary or secondary method is always visible when the receiver is of the type
to which the method is bound or of a subtype of such type.  Tertiary methods are
only visible if the module which defines them has been imported or used in such
a way that allows these methods to be called (see :ref:`Using_Modules` and
:ref:`Importing_Modules`).

.. note::

   Future work: the semantics of privacy specifiers for methods are still under
   discussion.

.. index::
   single: methods; receiver
   single: this
   single: classes; this
   single: records; this
   single: receiver
   single: type methods
   single: instance methods
   single: methods; type
   single: methods; instance
.. _Method_receiver_and_this:

The Method Receiver and the *this* Argument
-------------------------------------------

A method’s *receiver* is an implicit formal argument named ``this``
representing the expression on which the method is invoked. The
receiver’s actual argument is specified by the ``receiver-clause`` of a
method-call-expression as specified in :ref:`Method_Calls`.

   *Example (implicitThis.chpl)*.

   Let class ``C``, method ``foo``, and function ``bar`` be defined as
   

   .. code-block:: chapel

      class C {
        proc foo() {
          bar(this);
        }
      }
      proc bar(c: C) { writeln(c); }

   

   .. BLOCK-test-chapelpost

      var c1: C = new C();
      c1.foo();



   .. BLOCK-test-chapeloutput

      {}

   Then given an instance of ``C`` called ``c1``, the method call
   ``c1.foo()`` results in a call to ``bar`` where the argument is
   ``c1``. Within primary method ``C.foo()``, the (implicit) receiver
   formal has static type ``borrowed C`` and is referred to as ``this``.

Methods whose receivers are objects are called *instance methods*.
Methods may also be defined to have ``type`` receivers—these are known
as *type methods*.

Methods on a class ``C`` generally use a ``this`` type of ``borrowed C``
but ``this`` will be more generic in some cases. See
:ref:`Class_Methods`.

The optional ``this-intent`` is used to specify type methods, to
constrain a receiver argument to be a ``param``, or to specify how the
receiver argument should be passed to the method.

When no ``this-intent`` is used, a default this intent applies. For
methods on classes and other primitive types, the default this intent is
the same as the default intent for that type. For record methods, the
intent for the receiver formal argument is ``const``.
See :ref:`The_Default_Intent`.

A method whose ``this-intent`` is ``type`` defines a *type method*. It
can only be called on the type itself rather than on an instance of the
type. When ``this-intent`` is ``param``, it specifies that the function
can only be applied to param objects of the given type binding.

   *Example (paramTypeThisIntent.chpl)*.

   In the following code, the ``isOdd`` method is defined with a
   ``this-intent`` of ``param``, permitting it to be called on params
   only. The ``size`` method is defined with a ``this-intent`` of
   ``type``, requiring it to be called on the ``int`` type itself, not
   on integer values. 

   .. code-block:: chapel

      proc param int.isOdd() param {
        return this & 0x1 == 0x1;
      }

      proc type int.size() param {
        return 64;
      }

      param three = 3;
      var seven = 7;

      writeln(42.isOdd());          // prints false
      writeln(three.isOdd());       // prints true
      writeln((42+three).isOdd());  // prints true
      // writeln(seven.isOdd());    // illegal since 'seven' is not a param

      writeln(int.size());          // prints 64
      // writeln(42.size());        // illegal since 'size()' is a type method

   

   .. BLOCK-test-chapeloutput

      false
      true
      true
      64

Type methods can also be iterators.

   *Example (typeMethodIter.chpl)*.

   In the following code, the class ``C`` defines a type method iterator
   which can be invoked on the type itself: 

   .. code-block:: chapel

      class C {
        var x: int;
        var y: string;

        iter type myIter() {
          yield 3;
          yield 5;
          yield 7;
          yield 11;
        }
      }

      for i in C.myIter() do
        writeln(i);

   

   .. BLOCK-test-chapeloutput

      3
      5
      7
      11

When ``this-intent`` is ``ref``, the receiver argument will be passed by
reference, allowing modifications to ``this``. If ``this-intent`` is
``const ref``, the receiver argument is passed by reference but it
cannot be modified inside the method. The ``this-intent`` can also
describe an abstract intent as follows. If it is ``const``, the receiver
argument will be passed with ``const`` intent. If it is left out
entirely, the receiver will be passed with a default intent. The default
``this`` intent matches the default argument intent described in
:ref:`The_Default_Intent`.

   *Example (refThisIntent.chpl)*.

   In the following code, the ``doubleMe`` function is defined with a
   ``this-intent`` of ``ref``, allowing variables of type ``int`` to
   double themselves. 

   .. code-block:: chapel

      proc ref int.doubleMe() { this *= 2; }

   

   .. BLOCK-test-chapelpost

      var x: int = 2;
      x.doubleMe();
      writeln(x);

   

   .. BLOCK-test-chapeloutput

      4

   Given a variable ``x = 2``, a call to ``x.doubleMe()`` will set ``x``
   to ``4``.

.. index::
   single: methods; implicit this.
.. _Implicit_this_dot_in_methods:

Implicit *this.* in Methods
---------------------------

Within a method, an identifier can implicitly refer to a field or another
method. The compiler will implicitly add a ``this.`` in such cases.

   *Example (implicitThis.chpl)*.

   In the below example, within ``proc R.method()``, the identifiers ``field``,
   ``parenlessMethod``, and ``parenfulMethod`` will implicitly refer to
   ``this.field``, ``this.parenlessMethod``, and ``this.parenfulMethod``.

   .. code-block:: chapel

      record R {
        var field: int = 1;
        proc parenlessMethod { return 10; }
        proc parenfulMethod() { return 100; }
      }

      proc R.method() {
        var x = field + parenlessMethod + parenfulMethod();
        // the above behaves the same as the following:
        // var x = this.field + this.parenlessMethod + this.parenfulMethod();
        writeln(x);
      }

   .. BLOCK-test-chapelpost

      var r: R;
      r.method();

   .. BLOCK-test-chapeloutput

      111

When considering what an identifier might refer to in a method, the
compiler will consider scopes and parent scopes in turn and choose the
closest applicable match. During this process, it will consider fields
and methods available from the receiver type's definition point just
after considering a method scope.  This process does not apply to
parenful method calls; instead those are handled through overload
resolution (see :ref:`Determining_Most_Specific_Functions`).

   *Example (shadowingAndImplicitThis.chpl)*.

   In the below example, within ``proc R.method()``, the identifiers
   ``a``, ``b``, and ``c`` could all refer to a field or to a variable.
   In the example, the variables ``a`` and ``b`` are considered closer
   than the fields, but the variable ``c`` is considered further away.

   .. code-block:: chapel

      record R {
        var a: int = 100;
        var b: int = 10;
        var c: int = 1;
      }

      var c: int = 2;

      proc R.method(b=20) {
        var a = 200;

        var x = a;
        // 'a' here refers to the local variable 'a', because the lookup
        // process considers the method body before considering
        // fields and methods.

        var y = b;
        // 'b' here refers to the formal argument 'b', because the
        // lookup process considers formal arguments before considering
        // fields and methods.

        var z = c;
        // 'c' here refers to 'this.c', because the lookup process
        // considers fields and methods just after reaching the method
        // declaration. Since a match is found with the field, it is used
        // before the 'var c' declared outside this method is considered.

        writeln(x+y+z);
      }

   .. BLOCK-test-chapelpost

      var r: R;
      r.method();

   .. BLOCK-test-chapeloutput

      221

.. index::
   single: methods; operators
   single: operators; methods
.. _Operator_Methods:

Operator Methods
----------------

Operators may be overloaded (see :ref:`Function_Overloading`) to support new
behavior on one or more types using the ``operator`` keyword.  Such overloads
may be defined as standalone functions, e.g.

.. code-block:: chapel

   operator +(lhs: t1, rhs: t2) { ... }

or as methods defined on a particular type, e.g.

.. code-block:: chapel

   record R {
     var intField: int;

     operator +(lhs: R, rhs: R) {
       return new R(lhs.intField + rhs.intField);
     }
   }

Operator methods are equivalent to type methods - the type on which the operator
is declared causes the operator to have method-like visibility for that type,
and the ``this`` receiver is a ``type``.

Operator methods may be defined as primary, secondary, or tertiary methods.  For
example, the following code defines a primary ``+`` operator and a secondary
``-`` operator on a record:

.. code-block:: chapel

   record R {
     var intField: int;

     operator +(lhs: R, rhs: R) {
       return new R(lhs.intField + rhs.intField);
     }
   }

   operator R.-(lhs: R, rhs: R) {
     return new R(lhs.intField - rhs.intField);
   }


The method receiver for an operator method will be used to determine when that
operator is visible.  This behavior is most useful when the method receiver
matches the type of at least one of the other arguments to the operator.
However, it is possible to define an operator method where the receiver type
does not match the type for any other argument.

Operator methods can be defined on concrete types, generic types, or particular
instantiations of generic types.

A call to an operator - such as ``a + b`` which calls ``+`` - may resolve to any
visible operator method or standalone operator function.

.. index::
   single: operators; methods; visible
.. _Operator_Method_Visibility:

Operator Method Visibility
~~~~~~~~~~~~~~~~~~~~~~~~~~

Primary and secondary operator methods have similar visibility to other primary
and secondary methods.  In both cases, these methods can be viewed as part of
the type and will be available along with the type.  For regular methods, the
compiler searches for the method using the receiver's type (e.g. ``R`` in
``myR.method()`` supposing ``myR`` has type ``R``) definition point as well as
any type definition points for parent classes.  However, operator invocations
(such as ``a + b``) don't have a method receiver in the same way.  Instead, the
compiler uses the types of all the operator's arguments to find operator methods
defined along with the type.

As with other tertiary methods, ``import`` and ``use`` statements can be used to
control the visibility of tertiary operator methods.

.. index::
   single: operators; methods; candidates
.. _Determining_Operator_Candidate_Functions:

Determining Operator Candidate Functions
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When determining if an operator method or function is an appropriate candidate,
only the arguments to the operator method or function will be considered.  The
presence or absence of a type receiver is only used to determine visibility, and
it will not eliminate an overload from candidate consideration.

.. index::
   single: operators; methods; more specific
.. _Determining_More_Specific_Operators:

Determining More Specific Operators
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When determining which operator method or function is more specific, only the
arguments to the operator method or function will be considered.  The presence
or absence of a type receiver is only used to determine visibility and does not
impact the process of determining the best function (see
:ref:`Determining_Best_Functions`).

.. index::
   single: methods; indexing
   single: this
   single: methods; this
.. _The_this_Method:

The *this* Method
-----------------

A procedure method declared with the name ``this`` allows the receiver
to be “indexed” similarly to how an array is indexed. Indexing (as with
``A[1]``) has the semantics of calling a method named ``this``. There is
no other way to call a method called ``this``. The ``this`` method must
be declared with parentheses even if the argument list is empty.

   *Example (thisMethod.chpl)*.

   In the following code, the ``this`` method is used to create a class
   that acts like a simple array that contains three integers indexed by
   1, 2, and 3. 

   .. code-block:: chapel

      class ThreeArray {
        var x1, x2, x3: int;
        proc this(i: int) ref {
          select i {
            when 1 do return x1;
            when 2 do return x2;
            when 3 do return x3;
          }
          halt("ThreeArray index out of bounds: ", i);
        }
      }

   

   .. BLOCK-test-chapelpost

      var ta = new ThreeArray();
      ta(1) = 1;
      ta(2) = 2;
      ta(3) = 3;
      for i in 1..3 do
        writeln(ta(i));
      ta(4) = 4;

   

   .. BLOCK-test-chapeloutput

      1
      2
      3
      thisMethod.chpl:9: error: halt reached - ThreeArray index out of bounds: 4

.. index::
   single: methods; iterating
   single: these
   single: methods; these
.. _The_these_Method:

The *these* Method
------------------

An iterator method declared with the name ``these`` allows the receiver
to be “iterated over” similarly to how a domain or array supports
iteration. When a value supporting a ``these`` method is used as the
``iterable-expression`` of a loop, the loop proceeds in a manner
controlled by the ``these`` iterator.

   *Example (theseIterator.chpl)*.

   In the following code, the ``these`` method is used to create a class
   that acts like a simple array that can be iterated over and contains
   three integers. 

   .. code-block:: chapel

      class ThreeArray {
        var x1, x2, x3: int;
        iter these() ref {
          yield x1;
          yield x2;
          yield x3;
        }
      }

   

   .. BLOCK-test-chapelpost

      var ta = new ThreeArray();
      for (i, j) in zip(ta, 1..) do
        i = j;

      for i in ta do
        writeln(i);

   

   .. BLOCK-test-chapeloutput

      1
      2
      3

An iterator type method with the name ``these`` supports iteration over
the class type itself.

   *Example (typeMethodIterThese.chpl)*.

   In the following code, the class ``C`` defines a type method iterator
   named ``these``, supporting direct iteration over the type:
   

   .. code-block:: chapel

      class C {
        var x: int;
        var y: string;

        iter type these() {
          yield 1;
          yield 2;
          yield 4;
          yield 8;
        }
      }

      for i in C do
        writeln(i);

   

   .. BLOCK-test-chapeloutput

      1
      2
      4
      8

.. _Methods_without_Parentheses:

Methods without parentheses
---------------------------

Similarly to :ref:`Functions_without_Parentheses`, it is possible to
create methods that do not have parentheses. Such methods look similar to
field access when they are called. As a result, a method without
parentheses can be used to replace a field that was removed or renamed
while providing the same interface as the field accessor.

   *Example (parenlessMethod.chpl)*.

   For example, the following code shows a ``record myType`` that has a
   field ``x``. It also shows some code that operates on the field.

   .. BLOCK-test-chapelpre
     /*

   .. code-block:: chapel

      record myType {
        var x: int;
      }

      var v: myType;
      writeln(v.x);

   .. BLOCK-test-chapelpost
     */

   Now, suppose that as ``myType`` evolves, it is adjusted to compute the
   value of ``x`` without storing it at all. In that case, a
   method without parentheses can allow the code using ``myType`` to
   function as it did before:

   .. code-block:: chapel

      record myType {
        // this parentheses-less function supports
        // a field-access syntax
        proc x : int {
          return 0; // compute ``x`` and return it
        }
      }

      var v: myType;
      writeln(v.x);

   .. BLOCK-test-chapeloutput

      0

One can create a method without parentheses to replace a field or
parenless method in a parent class. Such methods require the ``override``
keyword (see :ref:`Overriding_Base_Class_Methods`).

Note that class methods without parentheses that return with ``type`` or
``param`` intent use a generic type for the ``this`` argument. See
:ref:`Class_Methods` for more details.

It is a redeclaration error to define a method without parentheses with
the same name as a field.
