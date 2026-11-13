# TODO

What is pending in the port of SNKRX (`261111-snkrx.md`): settled items
leave this file, new open ones enter it, in the same commit when
possible.

## A. Decisions waiting for the author

1. The new name of the compiler. `fsantanna/ceu-atmos` is now
   `ia-atmos/lang`; `ext/ceu-atmos` still points to the old address
   (which redirects), and 35 mentions in 11 files use the old name
   (`CREDITS.md`, `README.md`, `run.sh`, `tools/setup.sh`,
   `build/one.sh`, `tst/run.sh`, `doc/compiler.md`, `doc/bench.md`,
   `doc/study.md`, `261111-snkrx.md`, `build/cg.out`). This repository
   may itself become `ia-atmos/sdl-snkrx`. Recommended: wait for the
   renames of the organization, then update all names in one commit,
   keeping the folder `ext/ceu-atmos` (fewer changes; the folder name
   is local).
2. A warning of the compiler for a spawn with no pool in a nested block
   (`doc/compiler.md`, 7), raised by the `ceu-atmos` session as a
   language decision. Recommended: yes, a warning; the port lost its
   attacks to it from S6 to S9, unseen.
3. `doc/compiler.md`, 1: may a pool or a task have a clock of its own?
   Recommended: no change needed by the port (it scales the global
   time); a clock per pool would make the slow motion simpler.
4. `doc/compiler.md`, 2: should the tables built inside a `pin` be
   pinned with it? Recommended: yes; the port broadcasts the contacts
   of the physics because of it.
5. `doc/compiler.md`, 6: does `await :all` on an empty pool end at once?
   Recommended: yes; the port keeps a guard meanwhile.

## B. Work known and not done

1. Left out of the port: the texts under the mouse (`InfoText`), the
   tutorial, the credits screen, dragging a unit in the party, the
   achievements and Steam, the buttons that open web pages, and the
   options for mouse control, the run timer, the window size, and the
   cooldowns and arrow drawn on the snake.
2. The new `await(n, :e)` of the compiler (also `watching n, :e` and
   `loop on n, :e`) could replace counters kept by hand in the port; not
   tried. Optional.
3. Why the original peaks at 946 MB was not investigated (`bench.md`).

## C. Reported and not acknowledged

1. All milestones done (S0 to S9), at `2757664`, with the summary and
   conclusions of the study (`doc/case-study.md`).
2. Finding 7 and its fix: the units of the port never attacked from S6
   until S9; fixed with pools, documented in `doc/compiler.md`.
3. The benchmark rerun (`41ae483`): the S7 stress numbers were measured
   on a port that did not attack and should not be compared.

## D. Older items

None.
