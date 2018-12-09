GBA DMA Autograder Template
===========================

For information on what this is and how it works, see:
<http://austinjadams.com/blog/autograding-gba-dma/>. In short, it's an
autograder for Gameboy Advance [DMA][1], a concept which plays an
important role in CS 2110 at Georgia Tech.

In this example assignment, students draw an image on the top-left
corner of the screen. It's a simplification of Timed Lab 06 from Fall
2018, the assignment for which I wrote all this DMA stuff.

The `grader/` directory contains the grader, which is the cool part (in
my opinion at least). The `student/` directory contains the directory
where students can work and do local testing with a Gameboy emulator.
(Warning: it requires the CS 2110 GBA toolchain, which I will not
explain here.) The `solution/` directory has the solution, which you can
copy to `shared/` to test the solution in both the grader and the
student directory.

[1]: https://en.wikipedia.org/wiki/Direct_memory_access
