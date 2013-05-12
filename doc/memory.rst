=======
Memory
=======

This is another example of doxygenfile output.

Configuration
------------------

.. doxygenstruct:: DRAM::Config

Scheduling
------------------

.. doxygenclass:: DRAM::MemoryController
   :members:

.. doxygenstruct:: DRAM::Transaction

.. doxygenenum:: DRAM::CommandType
.. doxygenstruct:: DRAM::Command

Hierarchy
------------------

.. doxygenclass:: DRAM::MemorySystem
   :members:
.. doxygenclass:: DRAM::Channel
.. doxygenclass:: DRAM::Rank
.. doxygenclass:: DRAM::Bank

Address Mapping
==================

.. doxygenstruct:: DRAM::AddressMapping
.. doxygenstruct:: DRAM::BitField
.. doxygenstruct:: DRAM::Coordinates

Timing
------------------

====  ====  ====  ====  ===================
Prev  Next  Rank  Bank  Minimum Timing
====  ====  ====  ====  ===================
A     A     s     s     tRC
A     A     s     d     tRRD
P     A     s     s     tRP
F     A     s     s     tRFC
A     R     s     s     tRCD-tAL
R     R     s     a     max{tBL,tCCD}
R     R     d     a     tBL+tRTRS
W     R     s     a     tCWD+tBL+tWTR
W     R     d     a     tCWD+tBL+tRTRS-tCAS
A     W     s     s     tRCD-tAL
R     W     a     a     tCAS+tBL+tRTRS-tCWD
W     W     s     a     max{tBL,tCCD}
W     W     d     a     tBL+tOST
A     P     s     s     tRAS
R     P     s     s     tAL+tBL+tRTP-tCCD
W     P     s     s     tAL+tCWD+tBL+tWR
F     F     s     a     tRFC
P     F     s     a     tRP
====  ====  ====  ====  ===================

A = row access; R = column read; W = column write; P = precharge; F = refresh; s = same; d = different; a = any

====  ====  ===================  ===================  ===================
Prev  Next  Channel              Rank                 Bank
====  ====  ===================  ===================  ===================
A     A                          tRRD/tFAW            tRC=tRAS+tRP
A     R                                               tRCD-tAL
A     W                                               tRCD-tAL   
A     P                                               tRAS  
R     P                                               tAL+tBL+tRTP-tCCD   
W     P                                               tAL+tCWD+tBL+tWR    
P     A                                               tRP   
R     R     tBL+tRTRS            max{tBL,tCCD}        (same as rank)
R     W     tCAS+tBL+tRTRS-tCWD  (same as channel)    (same as rank)
W     R     tCWD+tBL+tRTRS-tCAS  tCWD+tBL+tWTR        (same as rank) 
W     W     tBL+tOST             max{tBL,tCCD}        (same as rank)
P     F                          tRP                  (same as rank)
F     A                          tRFC                 (same as rank)
====  ====  ===================  ===================  ===================

Different contraints found in DRAMSim2

====  ====  ===============  ===============  ===========================
Prev  Next  Channel          Rank             Bank
====  ====  ===============  ===============  ===========================
R     P                                       tAL+tBL+max{tRTP,tCCD}-tCCD
Rp    A                                       tAL+tRTP+tRP
*     X                      tCKE             (same as rank)
X     *                      tXP              (same as rank)
====  ====  ===============  ===============  ===========================

.. doxygenstruct:: DRAM::Timing
.. doxygenstruct:: DRAM::ChannelTiming
.. doxygenstruct:: DRAM::RankTiming
.. doxygenstruct:: DRAM::BankTiming
