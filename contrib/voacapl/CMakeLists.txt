# VOACAP point-to-point HF propagation engine (voacapl by J. A. Watson),
# used by the Conditions window. The original VOACAP is US Government work
# (NTIA/ITS, not subject to copyright); voacapl's changes are CC0. See
# LICENSE in this directory.
#
# Built with its own plain Fortran flags: the bounds checking used for the
# rest of the program trips over this legacy code.

set (CMAKE_Fortran_FLAGS_RELEASE "-O2")
set (CMAKE_Fortran_FLAGS_DEBUG "-g -O0")
set (CMAKE_Fortran_FLAGS_RELWITHDEBINFO "-O2 -g")
set (CMAKE_Fortran_FLAGS_MINSIZEREL "-Os")
set (voacapl_flags -w -fno-backslash -ffixed-line-length-none -fno-sign-zero)
set (voacapl_mod_dir ${CMAKE_CURRENT_BINARY_DIR}/mod)

# All Fortran, so Qt's automoc (on for the rest of the program) has nothing
# to do here; left on, it adds a stub C++ file to each library. The flags
# below are also given to Fortran files only: GCC's C++ compiler lets -cpp
# and -fno-backslash through, but Apple's clang rejects them.
set (CMAKE_AUTOMOC OFF)
set (CMAKE_AUTOUIC OFF)
set (CMAKE_AUTORCC OFF)

add_library (voacapl_modules STATIC
  modules/voacapl_defs.for
  modules/crun_directory.f95
  modules/cversn.f95
  )
target_compile_options (voacapl_modules PRIVATE "$<$<COMPILE_LANGUAGE:Fortran>:-cpp;-w;-DVERSION=\"0.7.6\">")
set_target_properties (voacapl_modules PROPERTIES Fortran_MODULE_DIRECTORY ${voacapl_mod_dir})

add_library (voacapl_voalib STATIC
  voa_lib/dazel1.for
  voa_lib/deg2chr.for
  voa_lib/doscolr.for
  voa_lib/icoldos.for
  voa_lib/col_dos.for
  voa_lib/wincolr.for
  voa_lib/chardeg.for
  voa_lib/lcount.for
  voa_lib/lenchar.for
  voa_lib/rblankc.for
  voa_lib/suffix.for
  voa_lib/pwrcut.for
  )
add_library (voacapl_hfmufesw STATIC
  hfmufesw/agauss.for
  hfmufesw/cang.for
  hfmufesw/cmpinv.for
  hfmufesw/coll.for
  hfmufesw/csz1.for
  hfmufesw/ech.for
  hfmufesw/matinv.for
  hfmufesw/mufesgan.for
  hfmufesw/mufesint.for
  hfmufesw/mutual.for
  hfmufesw/onej.for
  hfmufesw/react.for
  hfmufesw/resist.for
  hfmufesw/sim.for
  hfmufesw/sqmult.for
  hfmufesw/zmut.for
  )
add_library (voacapl_wp10dwin STATIC
  wp10dwin/antcal.for
  wp10dwin/azmax.for
  wp10dwin/bessel.for
  wp10dwin/antinit2.for
  wp10dwin/ccirgain.for
  wp10dwin/curcal.for
  wp10dwin/gainorm.for
  wp10dwin/gainrel.for
  wp10dwin/len_trim.for
  wp10dwin/antfile.for
  wp10dwin/antmodel.for
  wp10dwin/ioninit.for
  wp10dwin/iongain.for
  wp10dwin/cisi.for
  wp10dwin/dazel0.for
  wp10dwin/logparm.for
  wp10dwin/parabol.for
  wp10dwin/parmprec.for
  wp10dwin/readant.for
  wp10dwin/refcof.for
  wp10dwin/setmaxga.for
  wp10dwin/surfim.for
  wp10dwin/trigfun.for
  wp10dwin/gainterp.for
  wp10dwin/gainterb.for
  wp10dwin/curtain.for
  wp10dwin/pattrn0.for
  wp10dwin/dbltrap.for
  wp10dwin/f2.for
  wp10dwin/invcon.for
  )
add_executable (voacapl
  voacapw/voacapw.for
  voacapw/blkdat.for
  voacapw/cancel.for
  voacapw/voacap.for
  voacapw/gettr.for
  voacapw/circant.for
  voacapw/read_asc.for
  voacapw/setvars.for
  voacapw/areamap.for
  voacapw/antcalc.for
  voacapw/allmodes.for
  voacapw/alosfv.for
  voacapw/anois1.for
  voacapw/babs.for
  voacapw/batch.for
  voacapw/batch_s.for
  voacapw/bendy.for
  voacapw/cngtim.for
  voacapw/convh.for
  voacapw/curmuf.for
  voacapw/dazel.for
  voacapw/decred.for
  voacapw/distxy.for
  voacapw/ef1var.for
  voacapw/esind.for
  voacapw/esmod.for
  voacapw/esreg.for
  voacapw/f2dis.for
  voacapw/f2var.for
  voacapw/fdist.for
  voacapw/findf.for
  voacapw/fixlin.for
  voacapw/flolin.for
  voacapw/fnorml.for
  voacapw/fobby.for
  voacapw/frqcom.for
  voacapw/gain.for
  voacapw/genfam.for
  voacapw/genion.for
  voacapw/genois.for
  voacapw/geom.for
  voacapw/geotim.for
  voacapw/gethp.for
  voacapw/getkmf.for
  voacapw/gettop.for
  voacapw/gmloss.for
  voacapw/gphbod.for
  voacapw/gridxy.for
  voacapw/harris.for
  voacapw/hfarea.for
  voacapw/hfciraf.for
  voacapw/outciraf.for
  voacapw/hfmufs.for
  voacapw/hfmufs2.for
  voacapw/inmuf.for
  voacapw/invcon.for
  voacapw/ionplt.for
  voacapw/ionset.for
  voacapw/lecden.for
  voacapw/listin.for
  voacapw/lngpat.for
  voacapw/luffy.for
  voacapw/magfin.for
  voacapw/magvar.for
  voacapw/monitr.for
  voacapw/mpath.for
  voacapw/noisy.for
  voacapw/nommuf.for
  voacapw/outall.for
  voacapw/outant.for
  voacapw/outarea.for
  voacapw/outbod.for
  voacapw/outgph.for
  voacapw/oution.for
  voacapw/outlay.for
  voacapw/outlin.for
  voacapw/outmuf.for
  voacapw/outpar.for
  voacapw/out_si.for
  voacapw/outtab.for
  voacapw/outtop.for
  voacapw/pen.for
  voacapw/penang.for
  voacapw/prbmuf.for
  voacapw/pwrdb.for
  voacapw/rdciraf.for
  voacapw/redmap.for
  voacapw/redaily.for
  voacapw/regmod.for
  voacapw/relbil.for
  voacapw/runfreqs.for
  voacapw/sang.for
  voacapw/selmod.for
  voacapw/seltxr.for
  voacapw/serprb.for
  voacapw/setgph.for
  voacapw/setlng.for
  voacapw/setluf.for
  voacapw/setout.for
  voacapw/settxr.for
  voacapw/sigdis.for
  voacapw/squeez.for
  voacapw/syssy.for
  voacapw/tabbod.for
  voacapw/tabs.for
  voacapw/timvar.for
  voacapw/txpwr.for
  voacapw/versy.for
  voacapw/virtim.for
  voacapw/xlimit6.for
  voacapw/xlin.for
  voacapw/zones.for
  )
foreach (target voacapl_voalib voacapl_hfmufesw voacapl_wp10dwin voacapl)
  target_compile_options (${target} PRIVATE "$<$<COMPILE_LANGUAGE:Fortran>:${voacapl_flags}>")
  target_include_directories (${target} PRIVATE ${voacapl_mod_dir} ${CMAKE_CURRENT_SOURCE_DIR}/voacapw)
  set_target_properties (${target} PROPERTIES Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/mod_${target})
  add_dependencies (${target} voacapl_modules)
endforeach ()
target_compile_options (voacapl PRIVATE "$<$<COMPILE_LANGUAGE:Fortran>:-cpp>")
target_link_libraries (voacapl voacapl_hfmufesw voacapl_voalib voacapl_wp10dwin voacapl_modules)

install (TARGETS voacapl
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT runtime
  )
