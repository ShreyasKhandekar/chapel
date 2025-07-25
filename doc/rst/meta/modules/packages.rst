.. _packages:

Package Modules
===============

Package modules are libraries that currently live outside of the
Chapel Standard Library, either because they are not considered to be
fundamental enough or because they are not yet mature enough for
inclusion there.  Over time, we expect many of these to become
``mason`` packages.

.. warning::

   All package modules are currently unstable and may change in the future.

Algorithms
----------
.. toctree::
   :maxdepth: 1

   Crypto <packages/Crypto>
   NPBRandom <packages/NPBRandom>
   Search <packages/Search>

Communication (Inter-Locale)
----------------------------
.. toctree::
   :maxdepth: 1

   AllLocalesBarriers <packages/AllLocalesBarriers>
   CopyAggregation <packages/CopyAggregation>
   MPI <packages/MPI>
   UnorderedAtomics <packages/UnorderedAtomics>
   UnorderedCopy <packages/UnorderedCopy>

Data Structures
---------------
.. toctree::
   :maxdepth: 1

   ConcurrentMap <packages/ConcurrentMap>
   DistributedBag <packages/DistributedBag>
   DistributedBagDeprecated <packages/DistributedBagDeprecated>
   DistributedDeque <packages/DistributedDeque>
   DistributedIters <packages/DistributedIters>
   LinkedLists <packages/LinkedLists>
   LockFreeQueue <packages/LockFreeQueue>
   LockFreeStack <packages/LockFreeStack>
   SortedMap <packages/SortedMap>
   SortedSet <packages/SortedSet>
   UnrolledLinkedList <packages/UnrolledLinkedList>

File Formats and I/O
--------------------
.. toctree::
   :maxdepth: 1

   ChplFormat <packages/ChplFormat>
   HDF5 <packages/HDF5>
   HDFS <packages/HDFS>
   Image <packages/Image>
   NetCDF <packages/NetCDF>
   ObjectSerialization <packages/ObjectSerialization>
   ParallelIO <packages/ParallelIO>
   PrecisionSerializer <packages/PrecisionSerializer>
   RecordParser <packages/RecordParser>
   TOML <packages/TOML>
   YAML <packages/YAML>
   Zarr <packages/Zarr>

Interoperability
----------------
.. toctree::
   :maxdepth: 1

   Dynamic Loading <packages/DynamicLoading>
   Python <packages/Python>

Math / Numerical Computing
--------------------------
.. toctree::
   :maxdepth: 1

   BLAS <packages/BLAS>
   FFTW <packages/FFTW>
   LAPACK <packages/LAPACK>
   LinearAlgebra <packages/LinearAlgebra>

Memory Management
-----------------
.. toctree::
   :maxdepth: 1

   Buffers <packages/Buffers>
   EpochManager <packages/EpochManager>


Networking / Inter-Process Communication
----------------------------------------
.. toctree::
   :maxdepth: 1

   Curl <packages/Curl>
   Protobuf <packages/ProtobufProtocolSupport>
   Socket <packages/Socket>
   URL <packages/URL>
   ZMQ <packages/ZMQ>

Parallel Computing
------------------
.. toctree::
   :maxdepth: 1

   AtomicObjects <packages/AtomicObjects>
   Channel <packages/Channel>
   Futures <packages/Futures>

Tool Support
------------
.. toctree::
   :maxdepth: 1

   UnitTest <packages/UnitTest>
   VisualDebug <packages/VisualDebug>

Utilities
---------
.. toctree::
   :maxdepth: 1

   ArgumentParser <packages/ArgumentParser>
   RangeChunk <packages/RangeChunk>

Miscellaneous
-------------
.. toctree::
   :maxdepth: 1

   Collection <packages/Collection>
   FunctionalOperations <packages/FunctionalOperations>
   PeekPoke <packages/PeekPoke>
   ReplicatedVar <packages/ReplicatedVar>
