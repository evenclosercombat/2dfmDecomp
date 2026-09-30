/*
 * cppunit.cpp - placeholder for the game's one C++ translation unit.
 *
 * The Rich header of the original records 13 C objects, 1 C++ object and 1 OMF object for the
 * game itself (LIBC accounts for the other 44 C, 1 C++ and 11 MASM objects).  Every byte of the
 * game's code and data is accounted for by the 13 C files, so whatever the C++ file contained
 * compiled to nothing that survived linking.  An empty C++ object reproduces the Rich header
 * exactly (entry order and counts) and, as a side effect, the offset of the PE header (0xF0; it
 * is 0xE0 without this object).
 *
 * It must come before at least one C object in the link, or the Rich entries come out in a
 * different order.
 */
