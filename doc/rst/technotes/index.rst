.. _technical_notes:

Technical Notes
===============

Technical Notes describe in-progress language features that are not
yet stable enough to be in the language specification. Additionally, a
Technical Note can describe implementation details that can be ignored
by most Chapel programmers.

Base Language Features
----------------------

.. toctree::
   :maxdepth: 1

   Associative Set Operations <sets>
   Avoiding Array Element Initialization with noinit <noinit>
   Error Handling Modes and Prototype Modules <errorHandling>
   First-class Procedures in Chapel <firstClassProcedures>
   Including Sub-Modules from Separate Files <module_include>
   The 'main()' Function <main>
   Module Search Paths <module_search>
   The 'manage' Statement <manage>
   Attributes <attributes>
   Interfaces <interfaces>
   Function-static Variables <static>
   Remote Variables <remote>

Initializers and Generic Programming
------------------------------------

.. toctree::
   :maxdepth: 1

   Forwarding Methods Calls <forwarding>
   Invoking Initializers with a Generic Instantiation <initTypeAlias>
   Partial Instantiations <partialInstantiations>
   Throwing Initializers <throwingInit>

Parallel Language Features
--------------------------

.. toctree::
   :maxdepth: 1

   Domain Map Standard Interface <dsi>
   The ‘local’ keyword <local>
   Querying a Local Subdomain <subquery>
   Reduce Intents <reduceIntents>
   Runtime Support for Atomics <atomics>
   The 'foreach' Loop <foreach>
   GPU Programming <gpu>

Interoperability
----------------

.. toctree::
   :maxdepth: 1

   C Interoperability <extern>
   Calling Chapel Code from Other Languages <libraries>
   Fortran Interoperability <fortranInterop>
   Using the Chapel Allocator from C <allocators>

IO
-----

.. toctree::
   :maxdepth: 1

   IO Serializers and Deserializers <ioSerializers>

Compiler Features
-----------------

.. toctree::
   :maxdepth: 1

   Checking for Nil Dereferences <nilChecking>
   Checking Overload Sets <overloadSets>
   Checking Variable Lifetimes <lifetimeChecking>
   Compiler Driver Mode <driver>
   Editions <editions>
   LLVM Support <llvm>
   Variables to Detect Compilation Configuration <globalvars>

Tool Details
------------

.. toctree::
   :maxdepth: 1

   Protocol Buffers Support - Generated Chapel Code <protoGenCodeGuide>


Performance Optimization
------------------------

.. toctree::
   :maxdepth: 1

   Optimizing Performance of Chapel Programs <optimization>
