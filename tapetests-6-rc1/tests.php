<?php

/*
 *  B-Em Tests
 *  (C) 2025 'Diminished'
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

  // Change directory to your B-Em directory
  // (containing the b-em executable), then run e.g.:

  //   /usr/bin/php -f ../tape_tests/tests.php brief

  declare(strict_types=1);
  $sep = ((PHP_OS_FAMILY == "Windows") ? "\\" : "/");
  define ("PATH_TO_TAPE_TESTS", pathinfo($_SERVER['argv'][0], PATHINFO_DIRNAME));

  // -----------------------------------------------------------------
  // ------------------- BEGIN PATHS CONFIGURATION -------------------
  // -----------------------------------------------------------------

  // We'll have to run from within the b-em directory,
  // because it needs to get at roms/ etc.
  
  // Use this by default; it should resolve to ".\b-em" on windows and "./b-em" on Unix
  define ("B_EM_EXE", ".".$sep."b-em");
  // specials:
  //   linux + valgrind:
  //define ("B_EM_EXE", "/usr/bin/valgrind --suppressions=../bem-suppressions-v7.txt --leak-check=full --gen-suppressions=yes ./b-em");
  //   linux + helgrind:
  // (note: can get suppressions from stderr output with this:) (will need minor edit for brackets at top and bottom)
  // $ cat /tmp/helgrind-vanilla-2.txt | grep '^[ ]\{3\}' | sed 's/   </}\n{\n   </g' > ../helgrind-suppressions.txt
  //define ("B_EM_EXE", "/usr/bin/valgrind --tool=helgrind --suppressions=../helgrind-suppressions-34.txt --gen-suppressions=all ./b-em");
  //   macOS w/DYLD_LIBRARY_PATH
  //define ("B_EM_EXE", "DYLD_LIBRARY_PATH=/opt/allegro-5.2.10.1/lib/ ./b-em"); // macOS

  // set this to a temporary directory that we can write to
  // for windows, we'll just use PATH_TO_TAPE_TESTS; for Linux and
  // MacOS, use /tmp
  define ("PATH_TO_TMPDIR", (PHP_OS_FAMILY == "Windows") ? PATH_TO_TAPE_TESTS : "/tmp");

  // -----------------------------------------------------------------
  // -------------------- END PATHS CONFIGURATION --------------------
  // -----------------------------------------------------------------

  define ("TESTS_VERSION", "6");
  define ("TESTS_DATE", "2nd November 2025");

  // this is used to pass debugger commands to B-Em, and should
  // be in some writeable temporary directory.
  // *REMEMBER* when adding new output files here, they must also be @unlink()ed
  // in delete_tmpfiles()
  define ("PATH_DEBUGGER_EXEC_TMPFILE", PATH_TO_TMPDIR.$sep."debugger-exec");
  define ("PATH_UEF_OPFILE",            PATH_TO_TMPDIR.$sep."my.uef");
  define ("PATH_UEF_UNCOMP_OPFILE",     PATH_TO_TMPDIR.$sep."my.unzuef");    // 3.2
  define ("PATH_CSW_UNCOMP_OPFILE",     PATH_TO_TMPDIR.$sep."my.unzcsw");    // 3.2
  define ("PATH_CSW_OPFILE",            PATH_TO_TMPDIR.$sep."my.csw");
  define ("PATH_TIBET_OPFILE",          PATH_TO_TMPDIR.$sep."my.tibet");
  define ("PATH_TIBETZ_OPFILE",         PATH_TO_TMPDIR.$sep."my.tibetz");
  define ("PATH_WAV_OPFILE",            PATH_TO_TMPDIR.$sep."my.wav");       // 5.0
  define ("PATH_SERIAL_OPFILE",         PATH_TO_TMPDIR.$sep."rs423out.txt"); // 4.0

  define ("PATH_SUBSET_UEF_METADATA",       "UEF-metadata");
  define ("PATH_SUBSET_SIMPLE",             "simple");
  define ("PATH_SUBSET_CSW",                "CSW");
  define ("PATH_SUBSET_BEEBJIT_SET",        "games".$sep."beebjit-set");
  define ("PATH_SUBSET_OTHER_PROTECTED",    "games".$sep."other-protected");
  define ("PATH_SUBSET_MAKEUEF_PARITY_FIX", "games".$sep."estra-makeuef-1.9-and-2.4");
  define ("PATH_SUBSET_UEF_MAIN",           "UEF-main");
  define ("PATH_SUBSET_ULTRON_SURPRISE",    "games".$sep."ultron-surprise");
  define ("PATH_SUBSET_TIBET_SET",          "TIBET");
  define ("PATH_SUBSET_TAPESAVE",           "tapesave");
  define ("PATH_SUBSET_TDRE",               "tdre");
  define ("PATH_SUBSET_SIMPLESLOW",         "simpleslow"); // for basic tests that take longer, not included in "brief"
  define ("PATH_SUBSET_COMMS",              "comms");
  define ("PATH_FRAG_CFGS",                 "cfgs");

  // TTv6
  define ("CFGFILE_STANDARD",               PATH_TO_TAPE_TESTS.$sep.PATH_FRAG_CFGS.$sep."standard.cfg");
  define ("CFGFILE_OS_0_1",                 PATH_TO_TAPE_TESTS.$sep.PATH_FRAG_CFGS.$sep."os_0_1.cfg");
  
  define ("TEST_NAME_STRING_PAD",           58);

  // B-Em error codes
  define ("X_OK",     0);
  define ("X_START",  1); // the legacy exit code
  define ("X_ERR",   10);
  define ("X_EOF",   11);
  define ("X_BP0",   12);
  define ("X_BP1",   13);
  define ("X_BP2",   14);
  define ("X_BP3",   15);
  define ("X_BP4",   16);
  define ("X_BP5",   17);
  define ("X_BP6",   18);
  define ("X_BP7",   19);
  define ("X_BPX",   20);
  define ("X_EXPR",  21);
  define ("X_404",   22); // TOHv4.1: uniquely identify file-not-found
  
  // Internal error codes
  define ("E_OK",                  0);
  define ("E_UEF_GZDECODE",        1);
  define ("E_UEF_MAGIC",           2);
  define ("E_UEF_VERSION",         3);
  define ("E_UEF_INSANE_TYPE",     4);
  define ("E_UEF_117_LEN",         5);
  define ("E_UEF_117_RATE",        6);
  define ("E_UEF_117_BAUD",        7);
  define ("E_UEF_117_COUNT",       8);
  define ("E_UEF_117_EXCESS",      9);
  define ("E_UEF_NOT_SAVED",      10);
  define ("E_SILENCE_AFTER_DATA", 11); // not E_UEF_.. because it's not UEF specific
  define ("E_PARSE_UEF_NO_INPUT", 12);
  define ("E_BAUD_RUN_LEN",       13);

  // new 3.2 symbols for error codes
  $errnames = array(X_OK   =>"fine",
                    X_START=>"init",
                    X_ERR  =>"err",
                    X_EOF  =>"eof",
                    X_BP0  =>"bp0",
                    X_BP1  =>"bp1",
                    X_BP2  =>"bp2",
                    X_BP3  =>"bp3",
                    X_BP4  =>"bp4",
                    X_BP5  =>"bp5",
                    X_BP6  =>"bp6",
                    X_BP7  =>"bp7",
                    X_EXPR =>"expr",
                    X_404  =>"FnF"); // TOHv4.1: distinguish file-not-found

  define("MAGIC_TAPETEST_STDOUT_LINE", "tapetest:");

  // command-line opts
  define ("CLI_OPT_VERBOSE",       "+v");
  define ("CLI_OPT_SLOW_STARTUP",  "+s");
  define ("CLI_OPT_SHOW_OUTPUT",   "+o");
  define ("CLI_OPT_SHOW_TAPECTRL", "+t");

  define ("CLI_MODE_BRIEF",       "brief");
  define ("CLI_MODE_FULL",        "full");
  define ("CLI_MODE_EXTRA",       "extra");

  define ("FILEMAGIC_UEF",        "UEF File!");
  define ("FILEMAGIC_CSW",        "Compressed Square Wave");
  define ("FILEMAGIC_TIBET",      "tibet");
  define ("FILEMAGIC_WAV",        "RIFF");

  //function case_insensitive_filesystem() : bool {
  //  return (PHP_OS_FAMILY == "Windows") || (PHP_OS_FAMILY == "Darwin"); // awful
  //}

  // potentially useful: this was used to append another arg ($cfg) to the run() and run_without_testing() functions:
  // cat ../../tapetests-6/tests.php | sed 's/^[ ]\+\(run\)\(.\+\)\();\)/  \1\2, $cfg);/g' > ../../tapetests-6/tests-2.php
  
  // stuff about the config file entries that affect logging:
  //
  // static const char log_section[]    = "logging";
  //
  //    to_file = get_config_string(log_section, "to_file", "FATAL,ERROR,WARNING,INFO,DEBUG");
  //    to_stderr = get_config_string(log_section, "to_stderr", "FATAL,ERROR,WARNING");
  //    to_msgbox = get_config_string(log_section, "to_msgbox", "FATAL,ERROR");
  //    log_fn = get_config_string(log_section, "log_filename", NULL);
  //    bool append = get_config_bool(log_section, "append", 1);

  class TestsState {

    // command-line config:
    public bool $verbose;
    public bool $spew;
    public bool $test_protected;
    public bool $test_simpleslow;
    public bool $test_tdre;
    public bool $slow_startup;
    public bool $show_tapectrl;

    // state of tests:
    public int $testnum   = 0; // test number
    public int $successes = 0; // successes count
    public int $skips     = 0; // skips count

    public int $start_time_s;

    public bool $skip_unimplemented_tests;

    public bool $run_tape_simple             = FALSE;
    public bool $run_tape_uef_main           = FALSE;
    public bool $run_tape_uef_metadata       = FALSE;
    public bool $run_tape_tibet_set          = FALSE;
    public bool $run_tape_beebjit_set        = FALSE;
    public bool $run_tape_makeuef_parity_fix = FALSE;
    public bool $run_tape_other_protected    = FALSE;
    public bool $run_tape_ultron_surprise    = FALSE;
    public bool $run_tape_saves              = FALSE;
    public bool $run_tape_csw                = FALSE;
    public bool $run_tdre                    = FALSE;
    public bool $run_comms                   = FALSE;
    public bool $run_simpleslow              = FALSE;


    function __construct() {
      $this->verbose                     = FALSE;
      $this->spew                        = FALSE;
      $this->skip_unimplemented_tests    = TRUE;
      $this->test_protected              = TRUE;
      $this->test_tdre                   = TRUE;
      $this->test_simpleslow             = TRUE;
      $this->slow_startup                = FALSE;
      $this->show_tapectrl               = FALSE;
      $this->run_tape_simple             = FALSE;
      $this->run_tape_uef_main           = FALSE;
      $this->run_tape_csw                = FALSE;
      $this->run_tape_uef_metadata       = FALSE;
      $this->run_tape_tibet_set          = FALSE;
      $this->run_tape_beebjit_set        = FALSE;
      $this->run_tape_makeuef_parity_fix = FALSE;
      $this->run_tape_other_protected    = FALSE;
      $this->run_tape_ultron_surprise    = FALSE;
      $this->run_tape_saves              = FALSE;
      $this->run_tdre                    = FALSE;
      $this->run_comms                   = FALSE;
      $this->run_simpleslow              = FALSE;
      $this->start_time_s = time();
    }


    function activateTests() {
      // Choose some tests to activate
      $this->run_tape_simple          = TRUE;
      $this->run_tape_uef_main        = TRUE;
      $this->run_tape_csw             = TRUE;
      $this->run_tape_uef_metadata    = TRUE;
      $this->run_tape_tibet_set       = TRUE;
      $this->run_tape_saves           = TRUE;
      $this->run_comms                = TRUE;
      if ($this->test_simpleslow) {
        $this->run_simpleslow           = TRUE;
      }
      if ( $this->test_tdre ) {
        $this->run_tdre                 = TRUE;
      }
      if ( $this->test_protected ) {
        $this->run_tape_beebjit_set        = TRUE;
        $this->run_tape_makeuef_parity_fix = TRUE;
        $this->run_tape_other_protected    = TRUE;
        $this->run_tape_ultron_surprise    = TRUE;
      }
    }



//    function activateTests() {
//      // Choose some tests to activate
//      $this->run_tape_simple          = FALSE;
//      $this->run_tape_uef_main        = FALSE;
//      $this->run_tape_csw             = FALSE;
//      $this->run_tape_uef_metadata    = FALSE;
//      $this->run_tape_tibet_set       = FALSE;
//      $this->run_tape_saves           = FALSE;
//      $this->run_comms                = FALSE;
//      if ($this->test_simpleslow) {
//        $this->run_simpleslow           = TRUE;
//      }
//      if ( $this->test_tdre ) {
//        $this->run_tdre                 = FALSE;
//      }
//      if ( $this->test_protected ) {
//        $this->run_tape_beebjit_set        = FALSE;
//        $this->run_tape_makeuef_parity_fix = FALSE;
//        $this->run_tape_other_protected    = FALSE;
//        $this->run_tape_ultron_surprise    = FALSE;
//      }
//    }

    function getElapsed() : int {
      return time() - $this->start_time_s;
    }

    function parseCli (array $argv) : bool {
      $have_spew              = FALSE;
      $have_verbose           = FALSE;
      $have_slow_startup      = FALSE;
      $have_tapectrl          = FALSE;
      $options_finished       = FALSE;
      for ($i=1; $i < count($argv); $i++) {

        $s = $argv[$i];
        $dupe = FALSE;

        if ( !   $options_finished
             && ($s == CLI_OPT_VERBOSE) ) {
          if ($have_verbose) {
            $dupe = TRUE;
          } else {
            $this->verbose = TRUE;
            $have_verbose = TRUE;
          }
        } else if ( !   $options_finished
                    && ($s == CLI_OPT_SLOW_STARTUP) ) {
          if ($have_slow_startup) {
            $dupe = TRUE;
          } else {
            $this->slow_startup = TRUE;
            $have_slow_startup = TRUE;
          }
        } else if ( !   $options_finished
                    && ($s == CLI_OPT_SHOW_OUTPUT) ) {
          if ($have_spew) {
            $dupe = TRUE;
          } else {
            $this->spew = TRUE;
            $have_spew = TRUE;
          }
        } else if (    ! $options_finished
                    && ($s == CLI_OPT_SHOW_TAPECTRL) ) {
          if ($have_tapectrl) {
            $dupe = TRUE;
          } else {
            $this->show_tapectrl = TRUE;
            $have_tapectrl = TRUE;
          }
      
        } else {
          // anything beyond this point must be a mode
          $options_finished = TRUE;
          if ($s == CLI_MODE_BRIEF) {
            $this->test_protected  = FALSE;
            $this->test_tdre       = FALSE;
            $this->test_simpleslow = FALSE;
          } else if ($s == CLI_MODE_FULL) {
            // use defaults ...
          } else if ($s == CLI_MODE_EXTRA) {
            $this->skip_unimplemented_tests = FALSE;
          } else {
            print "E: unknown mode: $s\n";
            return FALSE;
          }
        }

        if ($dupe) {
          print "E: duplicate argument: $s\n";
          return FALSE;
        }

      }

      if ( ! $options_finished ) {
        print "E: No mode was supplied.\n";
        return FALSE;
      }

      return TRUE;
    }


    static function usage(string $argv0) {
      $pad = 5;
      print "\nB-Em automated tape tests, v".TESTS_VERSION." by 'Diminished', ".TESTS_DATE."\n\n";
      print "Usage:\n".
            "    php -f $argv0 [options] <mode>\n\n".
            "where <mode> is mandatory, and must be one of:\n\n".
            "    ".str_pad(CLI_MODE_BRIEF, $pad)." : synthetic tests only, no protected games (~10 minutes)\n".
            "    ".str_pad(CLI_MODE_FULL,  $pad)." : as ".CLI_MODE_BRIEF.", plus protected games and longer tests (~1 hour)\n".
            "    ".str_pad(CLI_MODE_EXTRA, $pad)." : as ".CLI_MODE_FULL. ", plus tests with which B-Em is not yet compliant\n".
            "\n".
            "[options] may be one or more of:\n\n".
            "    ".str_pad(CLI_OPT_VERBOSE, $pad)             ." : show B-Em command lines for each test\n".
            "    ".str_pad(CLI_OPT_SHOW_OUTPUT, $pad)         ." : display the full output from b-em's console\n".
            "    ".str_pad(CLI_OPT_SLOW_STARTUP, $pad)        ." : delay startup (slower, aids stability; may be needed on recent Macs)\n".
            "    ".str_pad(CLI_OPT_SHOW_TAPECTRL, $pad)       ." : force tapectrl pop-up window\n".
            "\n";
    }


  }

  $st = new TestsState;

  $argv = $_SERVER['argv'];
  if ( ! $st->parseCli($argv) ) {
    $st::usage($argv[0]);
    return -1;
  }

  $st->activateTests();

  // ==========================================================

  // check output tmpfiles (debugger, tapes) can be created

  delete_tmpfiles();
  $a = 0;
  $a += (FALSE===@file_put_contents(PATH_DEBUGGER_EXEC_TMPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_UEF_OPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_CSW_OPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_UEF_UNCOMP_OPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_CSW_UNCOMP_OPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_TIBET_OPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_TIBETZ_OPFILE, " ")) ? 0 : 1;
  $a += (FALSE===@file_put_contents(PATH_SERIAL_OPFILE, " ")) ? 0 : 1;

  delete_tmpfiles();
  if ($a != 8) {
    print "\nFATAL: could not create one or more tempfiles. Check paths in tests.php. Aborting.\n";
    die();
  }

  if (!file_exists("roms")) {
    print "\nFATAL: current directory must be B-Em base (so it can find roms etc). Aborting.\n";
    die();
  }
  


  $xs  = 10;    // xs,  default expiry time, for -expire, in seconds
  $xs2 = 40;    // xs2, a longer expiry time
  $xs3 = 10*60; // xs3, enough for full-length game loads (10 minutes at 100%)

  $cfg = CFGFILE_STANDARD;

  // basic paradigm; use a breakpoint to force immediate shutdown with code 4
  // (this is the default if $debugger_exec is the empty string)
  $grp = "exec";
  run ($st, "", "", "", $grp, X_BP0, 0, "basic run+shutdown paradigm", FALSE, array(), 5, $cfg); // never skip
  // if the basic shutdown paradigm fails, there isn't much hope for the rest of the tests, so this is a fatality
  if ($st->successes == 0) {
    print "\nFATAL: Basic run-and-shutdown failed. Check paths in tests.php? Aborting.\n";
    die();
  }




  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_SIMPLE.$sep;
  $skip = ! $st->run_tape_simple;
  $grp = "simple";

  // check that &FE08 has a value of 8 on boot
  $d = "b 2000\nb 4000\nbx 0\nbx 1\npaste ?&2000=&12|M?&4000=&12|MIF ?&FE08=8 THEN CALL &2000 ELSE CALL &4000|M\nc\n";
  run ($st, $d, "", "", $grp, X_BP0, 3, "ACIA status at boot: ?&FE08=8", $skip, array(), 5, $cfg);
  // simple: does CALL &2000 so we can trap that
  // fa18 and fabb are MOS CFS error routines
  $d = "b 2000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste ?&2000=&12|M?&4000=&12|M*TAPE|MCH.\"\"|MCALL &4000|M\nc\n"; // double \n needed, no idea why
  // -tapetest 3 : quit on EOF and error, but don't autocat on start.
  run ($st, $d, $p."002__S2000__simple.uef",                                                  "", $grp, X_BP0, 3, "UEF (gzip)", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."003__S2000__simple-nogzip.uef",                                           "", $grp, X_BP0, 3, "UEF (no gzip)", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."004__S2000__simple.tibet",                                                "", $grp, X_BP0, 3, "TIBET", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."005__S2000__simple.tibetz",                                               "", $grp, X_BP0, 3, "TIBETZ", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."006__S2000__simple.csw",                                                  "", $grp, X_BP0, 3, "CSW type 2", $skip, array(), $xs, $cfg);
  $d = "b 1000\nb fa18\nb fabb\nb 2000\nbx 0\nbx 1\nbx 2\nbx 3\npaste PAGE=&E00|MNEW|M?&2000=&12|M?&1000=&12|M*TAPE|MCHAIN\"\"|MCALL &2000|M\nc\n";
  run ($st, $d, $p."007__S1000_F2000__simple-type1.csw",                                      "", $grp, X_BP0, 3, "CSW type 1", $skip, array(), $xs, $cfg);
  // test silence takes correct amount of real-world time; success &2000, fail &4000
  $d = "b 2000\nb 1000\nbx 0\nbx 1\npaste PAGE=&E00|MNEW|M?&2000=&12|M?&1000=&12|M*TAPE|MTIME=0|MCH.\"\"|MCALL &1000|M\nc\nc\n";
  run ($st, $d, $p."009__S2000_F1000__silence-duration-116.uef",                          "", $grp, X_BP0, 3, "UEF silence &116 duration", $skip, array(), $xs2, $cfg);
  run ($st, $d, $p."010__S2000_F1000__silence-duration-112.uef",                          "", $grp, X_BP0, 3, "UEF silence &112 duration", $skip, array(), $xs2, $cfg);
  run ($st, $d, $p."011__S2000_F1000__silence-duration.tibet",                            "", $grp, X_BP0, 3, "TIBET silence duration", $skip, array(), $xs, $cfg);
  // 3.2: test strip-silence-and-leader turbo option
  // &2000 is failure, &1000 is success; expects load between 3 and 6 seconds
  // TODO: test this mode with other filetypes too?
  $d = "b 1000\nb fa18\nb fabb\nb 2000\nbx 0\nbx 1\nbx 2\nbx 3\npaste ?&2000=&12|M?&1000=&12|M*TAPE|MTIME=0|MCHAIN\"\"|MCALL &2000|M\nc\n";
  run ($st, $d, $p."012__S1000_F2000__skip-leader.uef",                                       "", $grp, X_BP0, 3, "skip leader, disabled", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."013__S1000_F2000__skip-silence.uef",                                      "", $grp, X_BP0, 3, "skip silence, disabled", $skip, array(), $xs, $cfg);
  // now &1000 is failure, &2000 is success; expect load in < 3 seconds w/tapeskip
  $d = "b 2000\nb fa18\nb fabb\nb 1000\nbx 0\nbx 1\nbx 2\nbx 3\npaste ?&2000=&12|M?&1000=&12|M*TAPE|MTIME=0|MCHAIN\"\"|MCALL &1000|M\nc\n";
  // note that these tests are identical to skip-leader and skip-silence, but
  // the success and failure conditions are reversed since we now run with
  // tapeskip on
  run ($st, $d, $p."014__S2000_F1000__skip-leader-for-tapeskip.uef",                  "-tapeskip", $grp, X_BP0, 3, "skip leader, enabled", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."015__S2000_F1000__skip-silence-for-tapeskip.uef",                 "-tapeskip", $grp, X_BP0, 3, "skip silence, enabled", $skip, array(), $xs, $cfg);

  // same again, but 300 baud now
  $d = "b 2000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste ?&2000=&12|M?&1000=&12|M*TAPE3|MCH.\"\"|MCALL &1000\nc\n\n"; // double \n needed, no idea why
  // the UEF uses chunk &100 with baud rate change chunk &107.
  // TODO: 300 baud in other chunk types
  run ($st, $d, $p."016__S2000__simple-300baud.uef",                                          "", $grp, X_BP0, 3, "UEF @ 300 baud", $skip, array(), $xs2, $cfg);
  run ($st, $d, $p."017__S2000__simple-300baud.tibetz",                                       "", $grp, X_BP0, 3, "TIBETZ @ 300 baud", $skip, array(), $xs2, $cfg);
  run ($st, $d, $p."018__S2000__simple-300baud.csw",                                          "", $grp, X_BP0, 3, "CSW @ 300 baud", $skip, array(), $xs2, $cfg);

  // *******************************************************************************
  // Reminder that -tapetest 7 forces cat on startup, which throws a fatal _EOF, and
  // so the exit code will be 11 (X_EOF) after the tape has been scanned.
  // *******************************************************************************
  $match = get_5hour_contents_array();
  run ($st, "", $p."019__CHKCAT__5hour100.uef",                                           "", $grp, X_EOF, 7, "5 hours, check catalogue", $skip, $match, $xs, $cfg);

  $match = array(0=>"0:00:07 CITAS      0d00 00004b00 00004b00");
  run ($st, "", $p."020__CHKCAT__citadel4.tibet",                                         "", $grp, X_EOF, 7, "CITAS appears in tape catalogue", $skip, $match, $xs, $cfg);
  
//  die();
  // Can't get this to fail reliably. No good. :/
//  $d = "b 4000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste ?&4000=&12|M*TAPE|MCH.\"\"|M\nc\n\n";
//  run ($st, $d, $p."021__S4000__overclock-and-strip-if-no-leading-silence.tibet", "-fasttape -tapeskip", $grp, X_BP0, 3, "Twin turboload modes, no leading silence", $skip, array(), $xs2);

  $d = "b 4000\nb 5000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\ntapeseek 50%\npaste ?&4000=&12|M?&5000=&12|M*TAPE|MCH.\"\"|M\nc\n\n";
  run ($st, $d, $p."030__S5000_F4000__seek.uef", "", $grp, X_BP1, 3, "seek: percent + UEF", $skip, array(), $xs2, $cfg);
  $d = "b 4000\nb 5000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\ntapeseek 20\npaste ?&4000=&12|M?&5000=&12|M*TAPE|MCH.\"\"|M\nc\n\n";
  run ($st, $d, $p."030__S5000_F4000__seek.uef", "", $grp, X_BP1, 3, "seek: time + UEF", $skip, array(), $xs2, $cfg);
  $d = "b 4000\nb 5000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\ntapeseek 20\npaste ?&4000=&12|M?&5000=&12|M*TAPE|MCH.\"\"|M\nc\n\n";
  run ($st, $d, $p."031__S5000_F4000__seek.tibetz", "", $grp, X_BP1, 3, "seek: time + TIBETZ", $skip, array(), $xs2, $cfg);
  $d = "b 4000\nb 5000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\ntapeseek 20\npaste ?&4000=&12|M?&5000=&12|M*TAPE|MCH.\"\"|M\nc\n\n";
  run ($st, $d, $p."032__S5000_F4000__seek.csw", "", $grp, X_BP1, 3, "seek: time + CSW", $skip, array(), $xs2, $cfg);
  run ($st, "", $p."033__CHKCAT__multi-hr-114-24bitlen-is-dcb9e0.uef", "", $grp, X_EOF, 7, "Evil, 2-hour, single &114 chunk (check catalogue)", $skip, get_evil_114_contents_array(), $xs, $cfg);
  
  $d = "b 5000\nb 5001\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\ntapeseek 50%\npaste ?&5000=&12|M?&5001=&12|M*TAPE3|MCH.\"\"|M\nc\n\n";
  run ($st, $d, $p."034__S5001_F5000+eof__check_UEF_seek_scans_back_for_baud.uef","",$grp,X_BP1,3,"Check UEF seek scans backwards for baud chunk",$skip,array(),$xs2,$cfg);

  // this relies on tape eject provoking an EOF condition, and hence an exit with -tapetest 1.
  // This has changed in TOHv4.3; previously, since TOHv4, EOF had been suppressed for blank (i.e. ejected) tapes.
  // Not sure if this will cause any problems ...

  // break at 4000 will perform tapeeject ...
  $d="b 4000\nb 5000\nb fa18\nb fabb\nbx 1\nbx 2\nbx 3\npaste *TAPE|MCH.\"\"|M\nc\ntapeeject\npaste CH.\"\"|M\nc\n";
  run ($st, $d, $p."035__Seof_F5000__play_eject.uef", "", $grp, X_EOF, 3, "Basic eject during load", $skip, array(), $xs2, $cfg);



  // 3.2: added dedicated CSW-testing section
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_CSW.$sep;
  $skip = ! $st->run_tape_csw;
  $grp = "CSW";
  // None of these actually load anything from the tape; we only care about B-Em CSW loader errors.
  // As such we don't need to pass anything to the debugger; just use -tapetest 7 to force exit
  // after CSW has been parsed. Expect X_EOF for OK and X_ERR for FAIL.
  $d = "";
  run ($st, $d, $p."001__ERR__bad_magic.csw",                  "", $grp, X_ERR, 7, "bad magic",                                $skip, array(), $xs, $cfg);
// die();
  run ($st, $d, $p."002__OK__version_21.csw",                  "", $grp, X_EOF, 7, "v2.1",                                     $skip, array(), $xs, $cfg);
// die();
  run ($st, $d, $p."003__ERR__version_22.csw",                 "", $grp, X_ERR, 7, "v2.2",                                     $skip, array(), $xs, $cfg);
  run ($st, $d, $p."004__OK__rate_8000.csw",                   "", $grp, X_EOF, 7, "8000 Hz rate",                             $skip, array(), $xs, $cfg);
  run ($st, $d, $p."005__ERR__rate_7999.csw",                  "", $grp, X_ERR, 7, "7999 Hz rate",                             $skip, array(), $xs, $cfg);
  run ($st, $d, $p."006__OK__rate_192000.csw",                 "", $grp, X_EOF, 7, "192000 Hz rate",                           $skip, array(), $xs, $cfg);
  run ($st, $d, $p."007__ERR__rate_192001.csw",                "", $grp, X_ERR, 7, "192001 Hz rate",                           $skip, array(), $xs, $cfg);
  run ($st, $d, $p."008__OK__many_pulses.csw",                 "", $grp, X_EOF, 7, "many pulses",                              $skip, array(), $xs, $cfg);
  run ($st, $d, $p."009__ERR__too_many_pulses.csw",            "", $grp, X_ERR, 7, "too many pulses",                          $skip, array(), $xs, $cfg);
  run ($st, $d, $p."010__ERR__body_too_long.csw",              "", $grp, X_ERR, 7, "body too long",                            $skip, array(), $xs, $cfg);
  run ($st, $d, $p."011__ERR__bad_compression_value_0.csw",    "", $grp, X_ERR, 7, "compression code=0",                       $skip, array(), $xs, $cfg);
  run ($st, $d, $p."012__ERR__bad_compression_value_3.csw",    "", $grp, X_ERR, 7, "compression code=3",                       $skip, array(), $xs, $cfg);
  run ($st, $d, $p."013__ERR__longpulse_value_under_256.csw",  "", $grp, X_ERR, 7, "5-byte pulse duration < 256",              $skip, array(), $xs, $cfg);
  run ($st, $d, $p."014__OK__flags_7.csw",                     "", $grp, X_EOF, 7, "flags=7",                                  $skip, array(), $xs, $cfg);
  run ($st, $d, $p."015__ERR__flags_8.csw",                    "", $grp, X_ERR, 7, "flags=8",                                  $skip, array(), $xs, $cfg);
  run ($st, $d, $p."016__ERR__flags_16.csw",                   "", $grp, X_ERR, 7, "flags=16",                                 $skip, array(), $xs, $cfg);
  run ($st, $d, $p."017__ERR__flags_32.csw",                   "", $grp, X_ERR, 7, "flags=32",                                 $skip, array(), $xs, $cfg);
  run ($st, $d, $p."018__ERR__flags_64.csw",                   "", $grp, X_ERR, 7, "flags=64",                                 $skip, array(), $xs, $cfg);
  run ($st, $d, $p."019__ERR__flags_128.csw",                  "", $grp, X_ERR, 7, "flags=128",                                $skip, array(), $xs, $cfg);
  run ($st, $d, $p."020__ERR__pulses_mismatch.csw",            "", $grp, X_ERR, 7, "header/body pulse count mismatch",         $skip, array(), $xs, $cfg);
  run ($st, $d, $p."021__ERR__trunc_1.csw",                    "", $grp, X_ERR, 7, "header truncated",                         $skip, array(), $xs, $cfg);
  run ($st, $d, $p."022__OK__hdr_ext.csw",                     "", $grp, X_EOF, 7, "with header extension, permit puerile graffiti", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."023__ERR__hdr_ext_trunc.csw",              "", $grp, X_ERR, 7, "header extension truncated",                      $skip, array(), $xs, $cfg);
  // revert to standard CFS file loading tests for remaining CSWs (-tapetest 3)
  //$d = "b 1000\nbx 0\npaste ?&1000=&12|MCALL &1000|M\nc\n";
  run ($st, "", $p."024__no_pulses.csw",                       "", $grp, X_EOF,     7, "no pulses",                                $skip, array(), $xs, $cfg);
  $d = "b 2000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste ?&2000=&12|M?&1000=&12|M*TAPE|MCH.\"\"|MCALL &1000|M\nc\n\n"; // that \n\n again
  run ($st, $d, $p."025__S2000__simple_8000.csw",              "", $grp, X_BP0,     3, "load at 8000 Hz rate",                     $skip, array(), $xs, $cfg);
  run ($st, $d, $p."026__S2000__simple_192000.csw",            "", $grp, X_BP0,     3, "load at 192000 Hz rate",                   $skip, array(), $xs, $cfg);





  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_UEF_MAIN.$sep;
  $skip = ! $st->run_tape_uef_main;
  $grp = "UEF";


  // these UEFs contain a program which does CALL &1000;
  // we will also place a CALL &2000 into the Beeb input buffer stream,
  // so we can detect e.g. Bad Program which dumps us back to BASIC
  // we initialise both &1000 and &2000 with single RTS instructions
  $d = "b 1000\nb fa18\nb fabb\nb 2000\nbx 0\nbx 1\nbx 2\nbx 3\npaste ?&2000=&12|M?&1000=&12|M*TAPE|MCHAIN\"\"|MCALL &2000|M\nc\n";
  run ($st, $d, $p."001__S1000_F2000__datachunk100.uef",      "", $grp, X_BP0, 3, "Chunk &100: read data", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."002__S1000_F2000__datachunk102.uef",      "", $grp, X_BP0, 3, "Chunk &102: read data", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."003__S1000_F2000__datachunk104.uef",      "", $grp, X_BP0, 3, "Chunk &104: read data", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."004__S1000_F2000__datachunk114.uef",      "", $grp, X_BP0, 3, "Chunk &114: read data", $skip, array(), $xs, $cfg);
  run ($st, "", $p."005__OK__chunk114_PP.uef",                "", $grp, X_EOF, 7, "Chunk &114: permit <P, P> sequence", $skip, array(), $xs, $cfg);

  run ($st, "", $p."006__ERR__magic_truncated_1.uef",         "", $grp, X_ERR, 7, "Magic truncated (inside text)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."007__ERR__magic_truncated_2.uef",         "", $grp, X_ERR, 7, "Magic truncated (no terminator)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."008__ERR__header_truncated_1.uef",        "", $grp, X_ERR, 7, "Header truncated (ver. minor)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."009__ERR__header_truncated_2.uef",        "", $grp, X_ERR, 7, "Header truncated (ver. major)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."010__ERR__magic_unterminated_type_1.uef", "", $grp, X_ERR, 7, "Magic unterminated (type 1)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."011__ERR__magic_unterminated_type_2.uef", "", $grp, X_ERR, 7, "Magic unterminated (type 2)", $skip, array(), $xs, $cfg);

  run ($st, "", $p."012__OK__no_chunks.uef",                 "", $grp, X_EOF, 7, "Chunkless UEF is legal", $skip, array(), $xs, $cfg);
  run ($st, "", $p."013__OK__zero_length_chunk1.uef",        "", $grp, X_EOF, 7, "Permit zero-length chunk &1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."014__ERR__zero_length_chunk0.uef",       "", $grp, X_ERR, 7, "Reject zero-length chunk &0", $skip, array(), $xs, $cfg);

// die();
  
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_UEF_METADATA.$sep;
  $grp = "UEF-meta";
  $skip = ! $st->run_tape_uef_metadata;
  $skip2 = ($skip || $st->skip_unimplemented_tests);

// renumbering fun; see $[ $B + 22 ] for increment used
//                  copy-paste the run() lines below into list.txt
//for N in $(cat list.txt | grep state | tr ' ' ^) ; do N="$(echo $N | tr ^ ' ')" ; A=$(echo $N | sed -e 's/^run.\+[$]p[.]"\([^"]*\)".*$/\1/g') ; B="$(echo $A | sed 's/^\([0-9]\{3\}\)__.\+uef$/\1/g' | sed s/^0*''//g)" ; C=$(printf %03d $[ $B + 22 ]) ; D="`echo $N | sed -e 's/[0-9]\{3\}__/'$C'__/g'`" ; echo $D ; done

  run ($st, "", $p."001__OK__0000_origin__TERMINATED.uef",                          "", $grp, X_EOF, 7, "Chunk &0, origin: terminated", $skip, array(), $xs, $cfg);
  run ($st, "", $p."002__OK__0000_origin__UNTERMINATED.uef",                        "", $grp, X_EOF, 7, "Chunk &0, origin: unterminated", $skip, array(), $xs, $cfg);
  run ($st, "", $p."003__OK__0000_origin__MULTI_2.uef",                                "", $grp, X_EOF, 7, "Chunk &0, origin: multiple", $skip, array(), $xs, $cfg);
  run ($st, "", $p."004__OK__0000_origin__MULTI_128.uef",                              "", $grp, X_EOF, 7, "Chunk &0, origin: lots", $skip, array(), $xs, $cfg);
  run ($st, "", $p."005__ERR__0000_origin__MULTI_129.uef",                             "", $grp, X_ERR, 7, "Chunk &0, origin: too many", $skip, array(), $xs, $cfg);
  run ($st, "", $p."006__OK__0000_origin__UTF8_LEGAL.uef",                          "", $grp, X_EOF, 7, "Chunk &0, origin: legal UTF-8", $skip, array(), $xs, $cfg);
  run ($st, "", $p."007__ERR__0000_origin__UTF8_ILLEGAL_BAD_2_OCTET.uef",    "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (2-octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."008__ERR__0000_origin__UTF8_ILLEGAL_BAD_SEQ_IDENTIFIER.uef", "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (seq. ident.)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."009__ERR__0000_origin__UTF8_ILLEGAL_BAD_OCTET_2_OF_3.uef",         "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (2/3 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."010__ERR__0000_origin__UTF8_ILLEGAL_BAD_OCTET_3_OF_3.uef",         "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (3/3 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."011__ERR__0000_origin__UTF8_ILLEGAL_BAD_OCTET_2_OF_4.uef",         "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (2/4 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."012__ERR__0000_origin__UTF8_ILLEGAL_BAD_OCTET_3_OF_4.uef",         "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (3/4 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."013__ERR__0000_origin__UTF8_ILLEGAL_BAD_OCTET_4_OF_4.uef",         "", $grp, X_ERR, 7, "Chunk &0, origin: bad UTF-8 (4/4 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."014__OK__0001_instructions__TERMINATED.uef",                       "", $grp, X_EOF, 7, "Chunk &1, instructions: terminated", $skip, array(), $xs, $cfg);
  run ($st, "", $p."015__OK__0001_instructions__UNTERMINATED.uef",                     "", $grp, X_EOF, 7, "Chunk &1, instructions: unterminated", $skip, array(), $xs, $cfg);
  run ($st, "", $p."016__OK__0001_instructions__MULTI_2.uef",                          "", $grp, X_EOF, 7, "Chunk &1, instructions: multiple", $skip, array(), $xs, $cfg);
  run ($st, "", $p."017__OK__0001_instructions__MULTI_128.uef",                        "", $grp, X_EOF, 7, "Chunk &1, instructions: lots", $skip, array(), $xs, $cfg);
  run ($st, "", $p."018__ERR__0001_instructions__MULTI_129.uef",                       "", $grp, X_ERR, 7, "Chunk &1, instructions: too many", $skip, array(), $xs, $cfg);
  run ($st, "", $p."019__OK__0001_instructions__UTF8_LEGAL.uef",                       "", $grp, X_EOF, 7, "Chunk &1, instructions: legal UTF-8", $skip, array(), $xs, $cfg);
  run ($st, "", $p."020__ERR__0001_instructions__UTF8_ILLEGAL_BAD_2_OCTET.uef",        "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (2-octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."021__ERR__0001_instructions__UTF8_ILLEGAL_BAD_SEQ_IDENTIFIER.uef", "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (seq. ident.)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."022__ERR__0001_instructions__UTF8_ILLEGAL_BAD_OCTET_2_OF_3.uef",   "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (2/3 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."023__ERR__0001_instructions__UTF8_ILLEGAL_BAD_OCTET_3_OF_3.uef",   "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (3/3 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."024__ERR__0001_instructions__UTF8_ILLEGAL_BAD_OCTET_2_OF_4.uef",   "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (2/4 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."025__ERR__0001_instructions__UTF8_ILLEGAL_BAD_OCTET_3_OF_4.uef",   "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (3/4 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."026__ERR__0001_instructions__UTF8_ILLEGAL_BAD_OCTET_4_OF_4.uef",   "", $grp, X_ERR, 7, "Chunk &1, instructions: bad UTF-8 (4/4 octet)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."027__ERR__0001_instructions__TOO_LONG_10MB.uef",                   "", $grp, X_ERR, 7, "Chunk &1, instructions: too long (10MB)", $skip, array(), $xs, $cfg);
  
  run ($st, "", $p."028__OK__0003_inlay_scan__8bpp_1x1.uef",                                      "", $grp, X_EOF, 7, "Chunk &3, scan 1x1 @ 8bpp", $skip, array(), $xs, $cfg);
  run ($st, "", $p."029__OK__0003_inlay_scan__8bpp_1x1_greyscale.uef",                            "", $grp, X_EOF, 7, "Chunk &3, scan 1x1 @ 8bpp grey", $skip, array(), $xs, $cfg);
  run ($st, "", $p."030__ERR__0003_inlay_scan__8bpp_1x1__TOO_SHORT.uef",                          "", $grp, X_ERR, 7, "Chunk &3, scan 1x1 @ 8bpp, short", $skip, array(), $xs, $cfg);
  run ($st, "", $p."031__ERR__0003_inlay_scan__8bpp_1x1__TOO_LONG.uef",                           "", $grp, X_ERR, 7, "Chunk &3, scan 1x1 @ 8bpp, long", $skip, array(), $xs, $cfg);
  run ($st, "", $p."032__ERR__0003_inlay_scan__8bpp_1x1_greyscale__TOO_SHORT.uef",                "", $grp, X_ERR, 7, "Chunk &3, scan 1x1 @ 8bpp grey, short", $skip, array(), $xs, $cfg);
  run ($st, "", $p."033__ERR__0003_inlay_scan__8bpp_1x1_greyscale__TOO_LONG.uef",                 "", $grp, X_ERR, 7, "Chunk &3, scan 1x1 @ 8bpp grey, long", $skip, array(), $xs, $cfg);
  run ($st, "", $p."034__ERR__0003_inlay_scan__8bpp_0x0__NO_PIXELS.uef",                          "", $grp, X_ERR, 7, "Chunk &3, scan 0x0 @ 8bpp, empty", $skip, array(), $xs, $cfg);
  run ($st, "", $p."035__OK__0003_inlay_scan__24bpp_1x1.uef",                                     "", $grp, X_EOF, 7, "Chunk &3, scan 1x1 @ 24bpp", $skip, array(), $xs, $cfg);
  
  
  
  // v6: beware filename out of sequence
  run($st, "", $p."081__OK__0003_inlay_scan__citadel-TEST-INLAYS.uef","",$grp,X_EOF,5,"Chunk &3, Citadel Triple Threat",$skip,array(),$xs,$cfg);
  run ($st, "", $p."036__OK__0005_target_machine__model_b_any_keyboard.uef",                      "", $grp, X_EOF, 7, "Chunk &5, Model B + any kb", $skip, array(), $xs, $cfg);
  // note $skip2:
  run ($st, "", $p."037__ERR__0005_target_machine__model_b_any_keyboard__ZERO_LENGTH.uef",        "", $grp, X_ERR, 7, "Chunk &5, Model B + any kb, empty", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."038__ERR__0005_target_machine__model_b_any_keyboard__TOO_LONG.uef",           "", $grp, X_ERR, 7, "Chunk &5, Model B + any kb, long", $skip, array(), $xs, $cfg);
//die();
  run ($st, "", $p."039__ERR__0005_target_machine__model_b_any_keyboard__BAD_TOP_NYBBLE.uef",     "", $grp, X_ERR, 7, "Chunk &5, Model B + any kb, bad high nybble", $skip, array(), $xs, $cfg);
  run ($st, "", $p."040__ERR__0005_target_machine__model_b_any_keyboard__BAD_BOTTOM_NYBBLE.uef",  "", $grp, X_ERR, 7, "Chunk &5, Model B + any kb, bad low nybble", $skip, array(), $xs, $cfg);
  run ($st, "", $p."041__OK__0006_bitmux__1.uef",                                                 "", $grp, X_EOF, 7, "Chunk &6, bitmux = 1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."042__ERR__0006_bitmux__0__ZERO_MULTIPLIER.uef",                               "", $grp, X_ERR, 7, "Chunk &6, bitmux w/zero multiplier", $skip, array(), $xs, $cfg);
  run ($st, "", $p."043__ERR__0006_bitmux__16__STUPID_MULTIPLIER.uef",                            "", $grp, X_ERR, 7, "Chunk &6, bitmux w/stupid multiplier", $skip, array(), $xs, $cfg);
  run ($st, "", $p."044__OK__0007_extra_palette__256_entries.uef",                                "", $grp, X_EOF, 7, "Chunk &7, sane palette, 256 entries", $skip, array(), $xs, $cfg);
  // note $skip2:
  run ($st, "", $p."045__ERR__0007_extra_palette__0_entries__ZERO_LENGTH.uef",                    "", $grp, X_ERR, 7, "Chunk &7, empty palette", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."046__ERR__0007_extra_palette__2_entries__2ERO_LENGTH.uef",                    "", $grp, X_ERR, 7, "Chunk &7, effectively empty palette", $skip, array(), $xs, $cfg);
  run ($st, "", $p."047__OK__0008_rom_hint__present_named_exact.uef",                             "", $grp, X_EOF, 7, "Chunk &8, named ROM present, exact match", $skip, array(), $xs, $cfg);
  run ($st, "", $p."048__OK__0009_short_title.uef",                                               "", $grp, X_EOF, 7, "Chunk &9, title sane", $skip, array(), $xs, $cfg);
  run ($st, "", $p."049__OK__0009_short_title__no_terminator.uef",                                "", $grp, X_EOF, 7, "Chunk &9, title, unterminated", $skip, array(), $xs, $cfg);
  run ($st, "", $p."050__ERR__0009_short_title__TOO_LONG.uef",                                    "", $grp, X_ERR, 7, "Chunk &9, title, long", $skip, array(), $xs, $cfg);
  // note $skip2:
  run ($st, "", $p."051__ERR__0009_short_title__ZERO_LENGTH.uef",                                 "", $grp, X_ERR, 7, "Chunk &9, title, empty", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."052__OK__000A_visible_area.uef",                                              "", $grp, X_EOF, 7, "Chunk &A, vis. area sane", $skip, array(), $xs, $cfg);
  run ($st, "", $p."053__ERR__000A_visible_area__TOO_SHORT.uef",                                  "", $grp, X_ERR, 7, "Chunk &A, vis. area short", $skip, array(), $xs, $cfg);
  run ($st, "", $p."054__ERR__000A_visible_area__TOO_LONG.uef",                                   "", $grp, X_ERR, 7, "Chunk &A, vis. area long", $skip, array(), $xs, $cfg);
  // note $skip2 x 4:
  run ($st, "", $p."055__ERR__000A_visible_area__TOO_WIDE.uef",                                   "", $grp, X_ERR, 7, "Chunk &A, vis. area wide", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."056__ERR__000A_visible_area__TOO_TALL.uef",                                   "", $grp, X_ERR, 7, "Chunk &A, vis. area tall", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."057__ERR__000A_visible_area__ZERO_WIDTH.uef",                                 "", $grp, X_ERR, 7, "Chunk &A, vis. area zero width", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."058__ERR__000A_visible_area__ZERO_HEIGHT.uef",                                "", $grp, X_ERR, 7, "Chunk &A, vis. area zero height", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."059__OK__0115_phase_change__0.uef",                                           "", $grp, X_EOF, 7, "Chunk &115, phase = 0", $skip, array(), $xs, $cfg);
  // TOHv4.1: corrected filename
  run ($st, "", $p."060__ERR__0115_phase_change__361__TOO_LARGE.uef",                             "", $grp, X_ERR, 7, "Chunk &115, phase = 361", $skip, array(), $xs, $cfg);
  run ($st, "", $p."061__ERR__0115_phase_change__TOO_SHORT.uef",                                  "", $grp, X_ERR, 7, "Chunk &115, short phase", $skip, array(), $xs, $cfg);
  run ($st, "", $p."062__ERR__0115_phase_change__0__TOO_LONG.uef",                                "", $grp, X_ERR, 7, "Chunk &115, long phase", $skip, array(), $xs, $cfg);
  run ($st, "", $p."063__OK__0117_baud__300.uef",                                                 "", $grp, X_EOF, 7, "Chunk &117, 300 baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."064__OK__0117_baud__1200.uef",                                                "", $grp, X_EOF, 7, "Chunk &117, 1200 baud", $skip, array(), $xs, $cfg);
  // TOHv4.1: corrected filename
  run ($st, "", $p."065__ERR__0117_baud__1199__ILLEGAL_BAUD.uef",                                 "", $grp, X_ERR, 7, "Chunk &117, 1199 baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."066__OK__0120_position_marker.uef",                                           "", $grp, X_EOF, 7, "Chunk &120, position marker", $skip, array(), $xs, $cfg);
  run ($st, "", $p."067__OK__0120_position_marker__NO_TERMINATOR.uef",                            "", $grp, X_EOF, 7, "Chunk &120, position marker, unterminated", $skip, array(), $xs, $cfg);

  // note $skip2:
// $skip2=false;
  run ($st, "", $p."068__ERR__0120_position_marker__ZERO_LENGTH.uef",                             "", $grp, X_ERR, 7, "Chunk &120, position marker, empty", $skip2, array(), $xs, $cfg);
// die();
  run ($st, "", $p."069__OK__0130_tape_set_info__tape+channel_1_1.uef",                           "", $grp, X_EOF, 7, "Chunk &130, 1 tape, 1 channel", $skip, array(), $xs, $cfg);
  run ($st, "", $p."070__ERR__0130_tape_set_info__tape+channel_1_1__TOO_SHORT.uef",               "", $grp, X_ERR, 7, "Chunk &130, 1 tape, 1 channel, short", $skip, array(), $xs, $cfg);
  run ($st, "", $p."071__ERR__0130_tape_set_info__tape+channel_1_1__TOO_LONG.uef",                "", $grp, X_ERR, 7, "Chunk &130, 1 tape, 1 channel, long", $skip, array(), $xs, $cfg);
  run ($st, "", $p."072__ERR__0130_tape_set_info__5_1_1__BAD_VOCAB.uef",                          "", $grp, X_ERR, 7, "Chunk &130, illegal vocab", $skip, array(), $xs, $cfg);
  run ($st, "", $p."073__ERR__0130_tape_set_info__tape+channel_128_1__TOO_MANY_TAPES.uef",        "", $grp, X_ERR, 7, "Chunk &130, 128 tapes", $skip, array(), $xs, $cfg);
  run ($st, "", $p."074__ERR__0130_tape_set_info__tape+channel_0_1__NO_TAPES.uef",                "", $grp, X_ERR, 7, "Chunk &130, no tapes", $skip, array(), $xs, $cfg);
  run ($st, "", $p."075__ERR__0130_tape_set_info__tape+channel_1_0__NO_CHANNELS.uef",             "", $grp, X_ERR, 7, "Chunk &130, no channels", $skip, array(), $xs, $cfg);
  run ($st, "", $p."076__OK__0131_start_of_tape_side__tape0_side0_channel0.uef",                  "", $grp, X_EOF, 7, "Chunk &130+&131, tape 0, side 0, channel 0", $skip, array(), $xs, $cfg);
  // note $skip2:
  run ($st, "", $p."077__ERR__0131_start_of_tape_side__tape0_side0_channel0__NO_TERMINATOR.uef",  "", $grp, X_ERR, 7, "Chunk &130+&131, text unterminated", $skip2, array(), $xs, $cfg);
  run ($st, "", $p."078__ERR__0131_start_of_tape_side__tape0_side0_channel0__TAPE_ID_OOB.uef",    "", $grp, X_ERR, 7, "Chunk &130+&131, bad tape ID", $skip, array(), $xs, $cfg);
  run ($st, "", $p."079__ERR__0131_start_of_tape_side__tape0_side0_channel0__CHANNEL_ID_OOB.uef", "", $grp, X_ERR, 7, "Chunk &130+&131, bad channel ID", $skip, array(), $xs, $cfg);
  run ($st, "", $p."080__ERR__0131_start_of_tape_side__tape0_side0_channel0__TOO_SHORT.uef",      "", $grp, X_ERR, 7, "Chunk &130+&131, short", $skip, array(), $xs, $cfg);

  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_TIBET_SET.$sep;
  $skip = ! $st->run_tape_tibet_set;
  $grp = "TIBET";

  // FIXME: succeeds when it should fail
  run ($st, "", $p."000__ERR__0_byte.tibet",                        "", $grp, X_ERR, 7, "Zero-byte file", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_VERSION_LINE_NOSPC (cannot find a space in the version line)
  run ($st, "", $p."001__ERR__1_byte.tibet",                        "", $grp, X_ERR, 7, "One-byte file", $skip, array(), $xs, $cfg);
  // smallest __OK
  run ($st, "", $p."002__OK__empty.tibet",                          "", $grp, X_EOF, 7, "Smallest legal", $skip, array(), $xs, $cfg);
  
  // TAPE_E_TIBET_BADCHAR:
  run ($st, "", $p."020__ERR__empty_low_badchar.tibet",             "", $grp, X_ERR, 7, "Bad character < 0x20", $skip, array(), $xs, $cfg);
  run ($st, "", $p."021__ERR__empty_high_badchar.tibet",            "", $grp, X_ERR, 7, "Bad character > 0x7e", $skip, array(), $xs, $cfg);
  run ($st, "", $p."022__ERR__empty_high_badchar_2.tibet",          "", $grp, X_ERR, 7, "Bad character > 0x7f", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_UNK_WORD:
  run ($st, "", $p."030__ERR__bad_word.tibet",                      "", $grp, X_ERR, 7, "Unrecognised word", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_FIELD_INCOMPAT:
  run ($st, "", $p."040__OK__hint_compat_silence_time.tibet",       "", $grp, X_EOF, 7, "Hints: silence + /time", $skip, array(), $xs, $cfg);
  run ($st, "", $p."041__ERR__hint_compat_silence_speed.tibet",     "", $grp, X_ERR, 7, "Hints: silence + /speed", $skip, array(), $xs, $cfg);
  run ($st, "", $p."042__ERR__hint_compat_silence_baud.tibet",      "", $grp, X_ERR, 7, "Hints: silence + /baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."043__ERR__hint_compat_silence_framing.tibet",   "", $grp, X_ERR, 7, "Hints: silence + /framing", $skip, array(), $xs, $cfg);
  run ($st, "", $p."044__ERR__hint_compat_silence_phase.tibet",     "", $grp, X_ERR, 7, "Hints: silence + /phase", $skip, array(), $xs, $cfg);
  run ($st, "", $p."050__OK__hint_compat_leader_time.tibet",        "", $grp, X_EOF, 7, "Hints: leader + /time", $skip, array(), $xs, $cfg);
  run ($st, "", $p."051__OK__hint_compat_leader_speed.tibet",       "", $grp, X_EOF, 7, "Hints: leader + /speed", $skip, array(), $xs, $cfg);
  run ($st, "", $p."052__ERR__hint_compat_leader_baud.tibet",       "", $grp, X_ERR, 7, "Hints: leader + /baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."053__ERR__hint_compat_leader_framing.tibet",    "", $grp, X_ERR, 7, "Hints: leader + /framing", $skip, array(), $xs, $cfg);
  run ($st, "", $p."054__ERR__hint_compat_leader_phase.tibet",      "", $grp, X_ERR, 7, "Hints: leader + /phase", $skip, array(), $xs, $cfg);
  // data span tests should all succeed
  run ($st, "", $p."060__OK__hint_compat_data_time.tibet",          "", $grp, X_EOF, 7, "Hints: data + /time", $skip, array(), $xs, $cfg);
  run ($st, "", $p."061__OK__hint_compat_data_speed.tibet",         "", $grp, X_EOF, 7, "Hints: data + /speed", $skip, array(), $xs, $cfg);
  run ($st, "", $p."062__OK__hint_compat_data_baud.tibet",          "", $grp, X_EOF, 7, "Hints: data + /baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."063__OK__hint_compat_data_framing.tibet",       "", $grp, X_EOF, 7, "Hints: data + /framing", $skip, array(), $xs, $cfg);
  run ($st, "", $p."064__OK__hint_compat_data_phase.tibet",         "", $grp, X_EOF, 7, "Hints: data + /phase", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DECIMAL_TOO_LONG:
  run ($st, "", $p."070__OK__float_long.tibet",                     "", $grp, X_EOF, 7, "Decimal: long", $skip, array(), $xs, $cfg);
  run ($st, "", $p."071__ERR__float_toolong.tibet",                 "", $grp, X_ERR, 7, "Decimal: too long", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_MULTI_DECIMAL_POINT
  run ($st, "", $p."072__ERR__float_multiple_points.tibet",         "", $grp, X_ERR, 7, "Decimal: 1.00.0", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_POINT_ENDS_DECIMAL
  run ($st, "", $p."073__ERR__float_point_at_end.tibet",            "", $grp, X_ERR, 7, "Decimal: 1.", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DECIMAL_BAD_CHAR
  run ($st, "", $p."074__ERR__float_bad_char.tibet",                "", $grp, X_ERR, 7, "Decimal: 1.3x", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_LONG_SILENCE
  run ($st, "", $p."075__OK__silence_long.tibet",                   "", $grp, X_EOF, 7, "Silence < TIBET_SILENCE_LEN_MAX", $skip, array(), $xs, $cfg);
  run ($st, "", $p."076__ERR__silence_toolong.tibet",               "", $grp, X_ERR, 7, "Silence > TIBET_SILENCE_LEN_MAX", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_INT_TOO_LONG
  run ($st, "", $p."080__OK__int_long.tibet",                       "", $grp, X_EOF, 7, "Integer: long", $skip, array(), $xs, $cfg);
  run ($st, "", $p."081__ERR__int_toolong.tibet",                   "", $grp, X_ERR, 7, "Integer: too long", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_INT_BAD_CHAR
  run ($st, "", $p."082__ERR__int_negative.tibet",                  "", $grp, X_ERR, 7, "Integer: negative/bad char", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_LONG_LEADER
  run ($st, "", $p."090__OK__leader_long.tibet",                    "", $grp, X_EOF, 7, "Leader: max. duration", $skip, array(), $xs, $cfg);
  run ($st, "", $p."091__ERR__leader_toolong.tibet",                "", $grp, X_ERR, 7, "Leader: excessive duration", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DUP_TIME
  run ($st, "", $p."100__ERR__time_twice.tibet",                    "", $grp, X_ERR, 7, "Hints: /time twice", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DUP_SPEED
  run ($st, "", $p."101__ERR__speed_twice.tibet",                   "", $grp, X_ERR, 7, "Hints: /speed twice", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DUP_BAUD
  run ($st, "", $p."102__ERR__baud_twice.tibet",                    "", $grp, X_ERR, 7, "Hints: /baud twice", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DUP_FRAMING
  run ($st, "", $p."103__ERR__framing_twice.tibet",                 "", $grp, X_ERR, 7, "Hints: /framing twice", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DUP_PHASE
  run ($st, "", $p."104__ERR__phase_twice.tibet",                   "", $grp, X_ERR, 7, "Hints: /phase twice", $skip, array(), $xs, $cfg);
  // test framings and TAPE_E_TIBET_BAD_FRAMING fatality
  run ($st, "", $p."110__OK__framing_7E2.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 7E2", $skip, array(), $xs, $cfg);
  run ($st, "", $p."111__OK__framing_7O2.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 7O2", $skip, array(), $xs, $cfg);
  run ($st, "", $p."112__OK__framing_7E1.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 7E1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."113__OK__framing_7O1.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 7O1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."114__OK__framing_8N2.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 8N2", $skip, array(), $xs, $cfg);
  run ($st, "", $p."115__OK__framing_8N1.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 8N1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."116__OK__framing_8E1.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 8E1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."117__OK__framing_8O1.tibet",                    "", $grp, X_EOF, 7, "Hints: Framing 8O1", $skip, array(), $xs, $cfg);
  run ($st, "", $p."118__ERR__framing_bad.tibet",                   "", $grp, X_ERR, 7, "Hints: Framing bad (8n1)", $skip, array(), $xs, $cfg);
  run ($st, "", $p."120__OK__time_large.tibet",                     "", $grp, X_EOF, 7, "Hints: Time large", $skip, array(), $xs, $cfg);
  run ($st, "", $p."121__ERR__time_toolarge.tibet",                 "", $grp, X_ERR, 7, "Hints: Time too large", $skip, array(), $xs, $cfg);
  // test bauds and TAPE_E_TIBET_BAD_BAUD fatality
  run ($st, "", $p."130__OK__baud_300.tibet",                       "", $grp, X_EOF, 7, "Hints: 300 baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."131__OK__baud_1200.tibet",                      "", $grp, X_EOF, 7, "Hints: 1200 baud", $skip, array(), $xs, $cfg);
  run ($st, "", $p."132__ERR__baud_800.tibet",                      "", $grp, X_ERR, 7, "Hints: 800 baud", $skip, array(), $xs, $cfg);
  // test phases, and TAPE_E_TIBET_BAD_PHASE fatalities
  run ($st, "", $p."140__OK__phase_0.tibet",                        "", $grp, X_EOF, 7, "Hints: phase 0", $skip, array(), $xs, $cfg);
  run ($st, "", $p."141__OK__phase_90.tibet",                       "", $grp, X_EOF, 7, "Hints: phase 90", $skip, array(), $xs, $cfg);
  run ($st, "", $p."142__OK__phase_180.tibet",                      "", $grp, X_EOF, 7, "Hints: phase 180", $skip, array(), $xs, $cfg);
  run ($st, "", $p."143__OK__phase_270.tibet",                      "", $grp, X_EOF, 7, "Hints: phase 270", $skip, array(), $xs, $cfg);
  run ($st, "", $p."144__ERR__phase_360.tibet",                     "", $grp, X_ERR, 7, "Hints: phase 360", $skip, array(), $xs, $cfg);
  // (this one hits TAPE_E_TIBET_INT_BAD_CHAR rather than _BAD_PHASE)
  run ($st, "", $p."145__ERR__phase_negative.tibet",                "", $grp, X_ERR, 7, "Hints: phase -1", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_SPEED_HINT_HIGH
  run ($st, "", $p."150__OK__speed_high.tibet",                     "", $grp, X_EOF, 7, "Hints: speed < maximum", $skip, array(), $xs, $cfg);
  run ($st, "", $p."151__ERR__speed_toohigh.tibet",                 "", $grp, X_ERR, 7, "Hints: speed > maximum", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_SPEED_HINT_LOW
  run ($st, "", $p."152__OK__speed_low.tibet",                      "", $grp, X_EOF, 7, "Hints: speed > minimum", $skip, array(), $xs, $cfg);
  run ($st, "", $p."153__ERR__speed_toolow.tibet",                  "", $grp, X_ERR, 7, "Hints: speed < minimum", $skip, array(), $xs, $cfg);

  // TAPE_E_TIBET_DATA_JUNK_FOLLOWS_START
  run ($st, "", $p."160__ERR__data_junk_follows_start.tibet",       "", $grp, X_ERR, 7, "Data: junk follows keyword", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DATA_JUNK_FOLLOWS_LINE
  // killed and replaced in tibet 0.5 / TOHv4-rc2:
  //run ($st, "", $p."161__ERR__data_junk_follows_line.tibet",        "", $grp, X_ERR, 7, "Data: junk follows tonechars", $skip, array(), $xs);
  // TAPE_E_TIBET_DATA_ILLEGAL_CHAR
  run ($st, "", $p."161__OK__data_all_legal_chars.tibet",           "", $grp, X_EOF, 7, "Data: legal chars", $skip, array(), $xs, $cfg);
  run ($st, "", $p."162__ERR__data_illegal_chars.tibet",            "", $grp, X_ERR, 7, "Data: illegal chars", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DATA_DOUBLE_PULSE
  run ($st, "", $p."163__ERR__data_double_pulse.tibet",             "", $grp, X_ERR, 7, "Data: double pulse", $skip, array(), $xs, $cfg);
  // these ones we will actually load fully because why not
  $d = "b 4000\nb fa18\nb fabb\nb 5000\nbx 0\nbx 1\nbx 2\nbx 3\npaste ?&4000=&12|M?&5000=&12|M*TAPE|MCH.\"A\"|MCALL &5000|M\nc\n\n";
  run ($st, $d, $p."164__S4000__data_word1_comment.tibet",             "", $grp, X_BP0, 3, "Data: word 1 comment", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."165__S4000__data_word2_comment.tibet",             "", $grp, X_BP0, 3, "Data: word 2 comment", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."166__S4000__data_word1-2_comment_psycho.tibet",    "", $grp, X_BP0, 3, "Data: psycho comment", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."167__S4000__data_space.tibet",                     "", $grp, X_BP0, 3, "Data: space within data (tibet 0.5)", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."168__S4000__data_double_space.tibet",              "", $grp, X_BP0, 3, "Data: multi space within data (tibet 0.5)", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."169__S4000__data_starting_space.tibet",            "", $grp, X_BP0, 3, "Data: space begins data line (tibet 0.5)", $skip, array(), $xs, $cfg);
  run ($st, $d, $p."170__S4000__data_word3_comment.tibet",             "", $grp, X_BP0, 3, "Data: word 3 comment", $skip, array(), $xs, $cfg);

  // TAPE_E_TIBET_DANGLING_TIME
  run ($st, "", $p."180__ERR__hints_dangling_time.tibet",           "", $grp, X_ERR, 7, "Hints: dangling time", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DANGLING_PHASE
  run ($st, "", $p."181__ERR__hints_dangling_phase.tibet",          "", $grp, X_ERR, 7, "Hints: dangling phase", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DANGLING_SPEED
  run ($st, "", $p."182__ERR__hints_dangling_speed.tibet",          "", $grp, X_ERR, 7, "Hints: dangling speed", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DANGLING_BAUD
  run ($st, "", $p."183__ERR__hints_dangling_baud.tibet",           "", $grp, X_ERR, 7, "Hints: dangling baud", $skip, array(), $xs, $cfg);
  // TAPE_E_TIBET_DANGLING_FRAMING
  run ($st, "", $p."184__ERR__hints_dangling_framing.tibet",        "", $grp, X_ERR, 7, "Hints: dangling framing", $skip, array(), $xs, $cfg);
  // 3.2: new TIBET version testing block
  run ($st, "", $p."200__ERR__bad_version_word.tibet",              "", $grp, X_ERR, 7, "Version: bad \"tibet\" word",          $skip, array(), $xs, $cfg);
  run ($st, "", $p."201__ERR__version_no_minor.tibet",              "", $grp, X_ERR, 7, "Version: minor missing",               $skip, array(), $xs, $cfg);
  run ($st, "", $p."202__ERR__version_no_major.tibet",              "", $grp, X_ERR, 7, "Version: major missing",               $skip, array(), $xs, $cfg);
  run ($st, "", $p."203__ERR__version_no_decimal_point.tibet",      "", $grp, X_ERR, 7, "Version: no decimal point",            $skip, array(), $xs, $cfg);
  run ($st, "", $p."204__ERR__version_double_decimal_point.tibet",  "", $grp, X_ERR, 7, "Version: double decimal point",        $skip, array(), $xs, $cfg);
  run ($st, "", $p."205__ERR__version_not_digits.tibet",            "", $grp, X_ERR, 7, "Version: not made of digits",          $skip, array(), $xs, $cfg);
  run ($st, "", $p."206__ERR__version_major_not_digit.tibet",       "", $grp, X_ERR, 7, "Version: major is not made of digits", $skip, array(), $xs, $cfg);
  run ($st, "", $p."207__ERR__version_minor_not_digit.tibet",       "", $grp, X_ERR, 7, "Version: minor is not made of digits", $skip, array(), $xs, $cfg);
  run ($st, "", $p."208__ERR__version_diff_major.tibet",            "", $grp, X_ERR, 7, "Version: major mismatch",              $skip, array(), $xs, $cfg);
  run ($st, "", $p."209__ERR__version_zero_zero.tibet",             "", $grp, X_ERR, 7, "Version: 0.0 illegal",                 $skip, array(), $xs, $cfg);
  run ($st, "", $p."210__ERR__version_new_minor.tibet",             "", $grp, X_ERR, 7, "Version: new minor illegal",           $skip, array(), $xs, $cfg);
  run ($st, "", $p."211__OK__version_old_minor.tibet",              "", $grp, X_EOF, 7, "Version: old minor OK",                $skip, array(), $xs, $cfg);
  run ($st, "", $p."212__OK__version_old_minor_padded.tibet",       "", $grp, X_EOF, 7, "Version: old padded minor OK",         $skip, array(), $xs, $cfg);
  run ($st, "", $p."213__ERR__version_major_toolong.tibet",         "", $grp, X_ERR, 7, "Version: major too long",              $skip, array(), $xs, $cfg);
  run ($st, "", $p."214__OK__version_major_long.tibet",             "", $grp, X_EOF ,7, "Version: major long",                  $skip, array(), $xs, $cfg);
  run ($st, "", $p."215__ERR__version_minor_toolong.tibet",         "", $grp, X_ERR, 7, "Version: minor too long",              $skip, array(), $xs, $cfg);
  run ($st, "", $p."216__OK__version_minor_long.tibet",             "", $grp, X_EOF, 7, "Version: minor long",                  $skip, array(), $xs, $cfg);
  run ($st, "", $p."217__OK__empty_matching_dup_version.tibet",     "", $grp, X_EOF, 7, "Version: concat: versions match",      $skip, array(), $xs, $cfg);
  run ($st, "", $p."218__ERR__empty_mismatching_dup_version.tibet", "", $grp, X_ERR, 7, "Version: concat: versions differ",     $skip, array(), $xs, $cfg);

  // v6

  $d = "b fa18\nb fabb\nbx 0\nbx 1\npaste *TAPE|M*.|M\nc\n\n";
  run ($st, $d, $p."219__OK__TOH4-3_spurious_nil.tibet",            "", $grp, X_EOF, 3, "Foul, repulsive spurious-nulls bug",   $skip, array(), $xs2, $cfg);



  $grp = "comms";
  $skip = ! $st->run_comms;
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_COMMS.$sep;
  


  // TOHv4-rc3: initialise &4000 properly
  $d = "b 4000\nbx 0\npaste ?&4000=&12|M*FX 3,5|MREM The lazy dog jumps over the quick brown fox.|MCALL &4000|M\nc\nc\nc";
  $expected = ">REM The lazy dog jumps over the quick brown fox.\n\r>CALL &4000\n\r"; //>"; // \n\r\n\r
  @unlink(PATH_SERIAL_OPFILE);
  $r = run_without_testing($st, $d, "", " -rs423file ".PATH_SERIAL_OPFILE, 0, PATH_SERIAL_OPFILE, $skip, $xs, $cfg);
  print_report_update_successes($st, $r==$expected, "opmatch", "nomatch", "RS423 to file", $grp, $skip);

  // v4.2
  $grp = "simpleslow";
  $skip = ! $st->run_simpleslow;
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_SIMPLESLOW.$sep;
  
  $d = "b 5000\nb 5001\nbx 0\nbx 1\npaste *TAPE|M?&5000=&12|M?&5001=&12|MCH.\"\"|MCALL &5000|M\nc\nc\n";
  run ($st, $d, $p."005__S5001_F5000_playback-at-19200-baud-consumes-time.uef",  "", $grp, X_BP1, 3, "19200 baud (ULA /64, ACIA /1) eats tape",  $skip, array(), $xs3, $cfg);

  $d = "b 4000\nb 5000\nbx 0\nbx 1\npaste *TAPE|M?&4000=&12|M?&5000=&12|MCH.\"A\"|MCALL &5000|M\nc\nc\nc\n";
//  $d = "b 4000\nbx 0\npaste *TAPE|MCH.\"A\"|M\nc\n\n";
  run ($st, $d, $p."002_long.tibetz",  "-fasttape -tapeskip", $grp, X_BP0, 3, "-fasttape -tapeskip w/TIBET (thx. Sazhen86)",  $skip, array(), $xs2, $cfg);

  // 4.2: moved into simpleslow from simple
  $d = "b 5000\nb fa18\nb fabb\nb 4000\nbx 0\nbx 1\nbx 2\nbx 3\npaste ?&4000=&12|M?&5000=&12|MCH.\"CLKSP2\"|MCALL &4000\nc\n\n";
  // TOHv4.1: bugfixed
  run ($st, $d, "", "-disc ".$p."001__S5000_F4000__clocksp2.ssd", $grp, X_BP0, 1, "CLOCKSP2", $skip, array(), $xs3, $cfg);

  // TTv6: from simple
  // the program loaded here will check to see if TIME < 6000 for fasttape; if so it succeeds at &3000, else fails at &1000
  $d = "b 3000\nb fa18\nb fabb\nb 1000\nbx 0\nbx 1\nbx 2\nbx 3\npaste PAGE=&E00|MNEW|M?&1000=&12|M?&3000=&12|M*TAPE|MTIME=0|MCH.\"\"|MCALL &1000|M\nc\nc\n";
  run ($st, $d, $p."003__S3000_F1000__overclock.tibetz",                                      "-fasttape", $grp, X_BP0, 3, "fasttape", $skip, array(), $xs3, $cfg);

  // TTv6: from simple
  $d = "b 5000\nb 6000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\npaste ?&6000=&12|M?&5000=&12|M*TAPE|MCH.\"\"|M\nc\n\n"; // double \n needed, no idea why
  run ($st, $d, $p."004__S5000_F6000__DCD-tests-for-replacement-ula-1.uef", "", $grp, X_BP0, 3, "DCD behaviour", $skip, array(), $xs3, $cfg);

  $grp = "tdre";
  $skip = ! $st->run_tdre;
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_TDRE.$sep;

  $d = "b 4000\nb 5000\nbx 0\nbx 1\npaste ?&4000=&12|M?&5000=&12|MCH.\"AUTOHOG\"|M11\nc\nc\nc";
  run($st, $d, "", " -disc ".$p."autohoglet.ssd", $grp, X_BP0, 0, "TDRE delay evenness, 1200 baud", $skip, array(), $xs3, $cfg);

  $d = "b 4000\nb 5000\nbx 0\nbx 1\npaste ?&4000=&12|Mpaste ?&5000=&12|MCH.\"AUTOHOG\"|M33|M\nc\nc\nc";
  run($st, $d, "", " -disc ".$p."autohoglet.ssd", $grp, X_BP0, 0, "TDRE delay evenness, 300 baud", $skip, array(), $xs3, $cfg);

  $d = "b 4000\nb 5000\nbx 0\nbx 1\npaste PAGE=&E00|MNEW|M?&4000=&12|M?&5000=&12|M*TAPE|MCH.\"TDRE2A2\"|M\nc\nc\nc\n";
  run($st, $d, $p."tdre2-ranges-auto2.uef", "", $grp, X_BP0, 0, "TDRE delay ranges, all dividers", $skip, array(), $xs3, $cfg);




  $grp = "tapesave";
  $skip = ! $st->run_tape_saves;
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_TAPESAVE.$sep;
  // these tests are mostly "manual control" using run_without_testing()




  // save UEF, obtain its contents in $g
  $a = array();
  $g = run_without_testing ($st, "", "", "-tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  if (!$skip) { $e = parse_uef($g, $a); }
  print_report_update_successes($st, !isset($a[0]), "nochnks", "havchnks", "-tapesave w/o -record emits UEF headers only", $grp, $skip); //, $xs2);
  
  // parse generated UEF
  $a = array();
  $g = run_without_testing ($st, "", "", "-record -tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = $skip ? E_OK : parse_uef($g, $a);
  print_report_update_successes($st, E_OK==$e, "parse ok", "parsebad", "Generate UEF, parse skeleton", $grp, $skip); //, $xs2);
  // confirm origin chunk is first
  print_report_update_successes($st, (E_OK==$e)&&isset($a[0])&&(0 == $a[0]['type']), "0 ok", "0 bad", "UEF origin chunk is first", $grp, $skip); //, $xs2);

// die();
  
  // save CSW, obtain its contents in $g
  $g = run_without_testing ($st, "", "", "-tapesave ".PATH_CSW_OPFILE, 0, PATH_CSW_OPFILE, $skip, $xs2, $cfg);
  print_report_update_successes($st, (0 === @strpos($g, FILEMAGIC_CSW)), "magic ok", "no magic", "Generate CSW, check magic", $grp, $skip); //, $xs2);
  
  // save TIBET, obtain its contents in $g
  $g = run_without_testing ($st, "", "", "-tapesave ".PATH_TIBET_OPFILE, 0, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  print_report_update_successes($st, (FALSE !== @strpos($g, FILEMAGIC_TIBET)), "magic ok", "no magic", "Generate TIBET, check magic", $grp, $skip); //, $xs2);
  
  // save TIBETZ, obtain its contents in $g, then gunzip it
  $g = run_without_testing ($st, "", "", "-tapesave ".PATH_TIBETZ_OPFILE, 0, PATH_TIBETZ_OPFILE, $skip, $xs2, $cfg);
  if (!$skip && (strlen($g)>0)) { $g = @gzdecode($g); }
  print_report_update_successes($st, (FALSE !== @strpos($g, FILEMAGIC_TIBET)), "magic ok", "no magic", "Generate TIBETZ, check magic", $grp, $skip);
  
  // save WAV, obtain its contents in $g
//  $g = run_without_testing ($st, "", "", "-tapesave ".PATH_WAV_OPFILE, 0, PATH_WAV_OPFILE, $skip, $xs2);
  $g = run_without_testing ($st, "", "", "-tapesave ".PATH_WAV_OPFILE, 0, PATH_WAV_OPFILE, $skip, $xs2, $cfg);
  print_report_update_successes($st, (FALSE !== @strpos($g, FILEMAGIC_WAV)), "magic ok", "no magic", "Generate WAV, check magic", $grp, $skip);
  
  // make sure WAV export fails for 5 hours of audio
  // (WAV size limit is ~1GB, which is about 3h9m)
  $g = run_without_testing ($st, "", PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_SIMPLE.$sep."019__CHKCAT__5hour100.uef", "-tapesave ".PATH_WAV_OPFILE, 7, PATH_WAV_OPFILE, $skip, $xs, $cfg);
  print_report_update_successes($st, (FALSE === @strpos($g, FILEMAGIC_WAV)), "no magic", "magic ok", "WAV too large", $grp, $skip);

  
  // TODO: make sure WAVs sum differently for different phase values ...
  $g  = run_without_testing ($st, "b 4000\nbx 0\npaste *TAPE|M10 MODE 7|MSAVE\"A\"|M|MCALL &4000|M\nc\n\n", "", "-record -tapesave ".PATH_WAV_OPFILE, 0, PATH_WAV_OPFILE, $skip, $xs2, $cfg);
  $g2 = run_without_testing ($st, "b 4000\nbx 0\npaste *TAPE|M10 MODE 7|MSAVE\"A\"|M|MCALL &4000|M\nc\n\n", "", "-tapewavcosine -record -tapesave ".PATH_WAV_OPFILE, 0, PATH_WAV_OPFILE, $skip, $xs2, $cfg);
  print_report_update_successes($st, (strlen($g)>0)&&(strlen($g2)>0)&&($g!=$g2), "no match", "match", "Check -tapewavcosine does something", $grp, $skip);

// die();

  // NOTE pastek rather than paste here, in order to be MOS 0.1 compatible:
  $d = "b 4000\nbx 0\npastek |M?&4000=&12|M*TAPE|MSAVE \"A\"|M|MCALL &4000|M\nc\n\n\n";
  
  $g = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_TIBET_OPFILE, 0, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $g = strtr($g, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "--------..--....--..------....--..--....end"));
  print_report_update_successes($st, $ok, "ok", "fail", "duplicate CRC behaviour (OS 1.2)", $grp, $skip);

  // NOTE CFGFILE_OS_0_1
  $g = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_TIBET_OPFILE, 0, PATH_TIBET_OPFILE, $skip, $xs2, CFGFILE_OS_0_1);
  $g = strtr($g, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "--------..--....--..------....--..--....------....--..--....end")); // second CRC byte is duplicated!
  print_report_update_successes($st, $ok, "ok", "fail", "duplicate CRC behaviour (OS 0.1)", $grp, $skip);

  // save a non-blank UEF, then use B-Em to load it
  $d_save = "b 4000\nbx 0\npaste *MOTOR 1|MPAGE=&1900|MNEW|M10 MODE 7|M20 CALL &4000|M*TAPE|MSAVE \"CRaDDoCK\"|M|M?&4000=&12|MCALL &4000|M\nc\n\n"; // double |M for RECORD then RETURN
  $craddock = array("0:00:05 CRaDDoCK   0016 ffff1900 ffff8023");
  $d = "paste *TAPE|M*CAT|M\nc\n\n";

  run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  // hack to deal with fact that run_without_testing() doesn't currently return a result code :|
  $tn = "Save UEF, load UEF";
  if (file_exists(PATH_UEF_OPFILE)) {
    run ($st, $d, PATH_UEF_OPFILE, "", $grp, X_EOF, 7, $tn, $skip, $craddock, $xs2, $cfg); // tapetest=7, check autocat
  } else {
    print_report_update_successes ($st, false, "", "No UEF", $tn, $grp, $skip);
  }

  // again, with uncompressed UEF
  run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_UEF_UNCOMP_OPFILE, 0, PATH_UEF_UNCOMP_OPFILE, $skip, $xs2, $cfg);
  //@unlink(PATH_UEF_OPFILE); // TOHv4
  if (!$skip && file_exists (PATH_UEF_UNCOMP_OPFILE) && (FALSE === @rename(PATH_UEF_UNCOMP_OPFILE, PATH_UEF_OPFILE))) {
    print "ERROR: could not rename temp UEF file (unz->z)\n";
    die();
  }
  // hack to deal with fact that run_without_testing() doesn't currently return a result code :|
  $tn = "Save UEF, load UEF (uncomp.)";
  if (file_exists(PATH_UEF_OPFILE)) {
    run ($st, $d, PATH_UEF_OPFILE, "", $grp, X_EOF, 7, $tn, $skip, $craddock, $xs2, $cfg); // tapetest=7, check autocat
  } else {
    print_report_update_successes ($st, false, "", "No UEF", $tn, $grp, $skip);
  }

// die();

  // save a non-blank CSW, then use b-em to load it
  run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_CSW_OPFILE, 0, PATH_CSW_OPFILE, $skip, $xs2, $cfg);
  if (!$skip && file_exists (PATH_CSW_OPFILE)) {
    run ($st, $d, PATH_CSW_OPFILE, "", $grp, X_EOF, 7, "Save CSW, load CSW", $skip, $craddock, $xs2, $cfg); // tapetest=7, check autocat
  } else {
    // CSW file was not generated
    print_report_update_successes ($st, FALSE, "savefail", "savefail", "Save CSW, load CSW", $grp, $skip);
  }
  
  $my_array = array(
    0=>"0:00:06 A          0080 0000c000 0000c000",
    1=>"0:00:17 B          0080 0000c100 0000c100"
  );
  $d2 = "b 2000\nbx 0\npaste ?&2000=&12|M*MOTOR 1|MTIME=0|MREPEAT UNTIL TIME>100|M*TAPE|M*SAVE A C000 C080|M|M*SAVE B C100 C180|M|MTIME=0|MREPEAT UNTIL TIME>100|M*MOTOR 0|MCALL &2000|M\nc\n"; // double |M for RECORD then RETURN
  // tapetest 8, check autocat AT SHUTDOWN:
  run ($st, $d2, "", "-record", $grp, X_BP0, 8, "SAVE, then check autocat", $skip, $my_array, $xs2, $cfg);

  // again, with uncompressed CSW
  run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_CSW_UNCOMP_OPFILE, 0, PATH_CSW_UNCOMP_OPFILE, $skip, $xs2, $cfg);
  if (!$skip && file_exists(PATH_CSW_UNCOMP_OPFILE) && (FALSE === @rename(PATH_CSW_UNCOMP_OPFILE, PATH_CSW_OPFILE))) {
    print "ERROR: could not rename temp CSW file (unz->z)\n";
    die();
  }
  if ( ! file_exists(PATH_CSW_OPFILE) ) {
    // yuck
    print_report_update_successes ($st, FALSE, "savefail", "savefail", "Save CSW, load CSW (uncompressed)", $grp, $skip);
  } else {
    run ($st, $d, PATH_CSW_OPFILE, "", $grp, X_EOF, 7, "Save CSW, load CSW (uncompressed)", $skip, $craddock, $xs2, $cfg); // tapetest=7, check autocat
  }
  
  // save a non-blank TIBET, then use b-em to load it
  run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_TIBET_OPFILE, 0, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  if ( ! file_exists(PATH_TIBET_OPFILE) ) {
    // yuck
    print_report_update_successes ($st, FALSE, "savefail", "savefail", "Save TIBET, load TIBET", $grp, $skip);
  } else {
    run ($st, $d, PATH_TIBET_OPFILE, "", $grp, X_EOF, 7, "Save TIBET, load TIBET", $skip, $craddock, $xs2, $cfg); // tapetest=7, check autocat
  }
  
  // save a non-blank TIBETZ, then use b-em to load it
  run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_TIBETZ_OPFILE, 0, PATH_TIBETZ_OPFILE, $skip, $xs2, $cfg);
  if ( ! file_exists(PATH_TIBETZ_OPFILE) ) {
    // yuck
    print_report_update_successes ($st, FALSE, "savefail", "savefail", "Save TIBETZ, load TIBETZ", $grp, $skip);
  } else {
    run ($st, $d, PATH_TIBETZ_OPFILE, "", $grp, X_EOF, 7, "Save TIBETZ, load TIBETZ", $skip, $craddock, $xs2, $cfg); // tapetest=7, check autocat
  }
  
  
  // Test 3.2's modified-117 behaviour:
  // 1. save 1200 baud; no 117 chunk should be produced, as 1200 is the default
  $d_save = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|MSAVE \"1200\"|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = (($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : expect_uef_117_sequence ($u, array()));
  print_report_update_successes ($st, E_OK==$e, "117 ok", "117 bad", "&117s: save 1200", $grp, $skip);
  
  
  // 2. save 1200 then 300; one 117 chunk should be produced at 300
  $d_save = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|MSAVE \"1200\"|M|M*TAPE3|MSAVE \"300\"|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : expect_uef_117_sequence ($u, array(0=>300));
  print_report_update_successes ($st, E_OK==$e, "117 ok", "117 bad", "&117s: save 1200, 300", $grp, $skip);
  
//  die();
  
  // 3. save 300 then 1200; two 117 chunks
  $d_save = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE3|MSAVE \"300\"|M|M*TAPE|MSAVE \"1200\"|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d_save, "", "-record -tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : expect_uef_117_sequence ($u, array(0=>300, 1=>1200));
  print_report_update_successes ($st, E_OK==$e, "117 ok", "117 bad", "&117s: save 300, 1200", $grp, $skip);
  
  
  // 4. Load (1200, 300); append (300, 1200); expect 2x 117s total (legacy 300, appended 1200)
  $d_save = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE3|MSAVE \"300\"|M|M*TAPE|MSAVE \"1200\"|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d_save, $p."117s_1200_then_300.uef", "-record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs3, $cfg);
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : expect_uef_117_sequence ($u, array(0=>300, 1=>1200));
  print_report_update_successes ($st, E_OK==$e, "117 ok", "117 bad", "&117s: load 1200, 300; append 300, 1200", $grp, $skip);
  
  // 5. Load (1200, 300); append (1200, 300); expect 3x 117s total (legacy 300, appended 1200 then 300)
  $d_save = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|MSAVE \"1200\"|M|M*TAPE3|MSAVE \"300\"|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d_save, $p."117s_1200_then_300.uef", "-record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : expect_uef_117_sequence ($u, array(0=>300, 1=>1200, 2=>300));
  print_report_update_successes ($st, E_OK==$e, "117 ok", "117 bad", "&117s: load 1200, 300; append 1200, 300", $grp, $skip);
  
//  die();
  
  // 6. Save 4 blocks at 1200 baud with -tape117 and ensure 5 x &117s appear
  //    (that's one per block, plus one for the BUGFIX "squawk")
  $d_save = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|M*SAVE \"-tape117\" C000 +400|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d_save, "", "-tape117 -record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : expect_uef_117_sequence ($u, array(0=>1200, 1=>1200, 2=>1200, 3=>1200, 4=>1200));
  print_report_update_successes ($st, E_OK==$e, "117 ok", "117 bad", "&117s: -tape117", $grp, $skip);
  
  // Test num. origin chunks produced on UEF append with and without -tapeno0 (i.e. "do not emit origin chunk when appending to loaded UEF"):
  // a) without -tapeno0, expect 2 origin chunks
  $d = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|MSAVE \"A\"|M|MCALL &4000|M\nc\n\n";
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_SIMPLE.$sep; // just borrow the simple test again
  $u = run_without_testing ($st, $d, $p."002__S2000__simple.uef", "-record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $orgns = -1;
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : get_origin_000_count($u, $orgns); // $orgns populated with number of origin chunks
  print_report_update_successes ($st, (E_OK==$e)&&(2==$orgns), $orgns."xOrigin", $orgns."xOrigin", "without -tapeno0, 2 origin chunks", $grp, $skip);
  
  // b) with -tapeno0, expect 1 origin chunk
  $d = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|MSAVE \"A\"|M|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d, $p."002__S2000__simple.uef", "-tapeno0 -record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $orgns = -1;
  $e = ($skip||(strlen($u)==0)) ? E_UEF_NOT_SAVED : get_origin_000_count($u, $orgns); // $orgns populated with number of origin chunks
  print_report_update_successes ($st, (E_OK==$e)&&(1==$orgns), $orgns."xOrigin", $orgns."xOrigin", "with -tapeno0, 1 origin chunk", $grp, $skip);
  
  // Test compression & not-compression using .unzuef, .unzcsw extensions
  $d = "b 4000\nbx 0\npaste ?&4000=&12|M*TAPE|M*SAVE LOL C000 +800|M|MCALL &4000|M\nc\n\n";
  $nocomp = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_UEF_UNCOMP_OPFILE, 2, PATH_UEF_UNCOMP_OPFILE, $skip, $xs3, $cfg);
  $comp   = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs3, $cfg);
  print_report_update_successes ($st, strlen($comp)<strlen($nocomp), "csz<usz", "csz>=usz", "UNZUEF: compressed size < uncompressed size", $grp, $skip);
  
  $nocomp = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_CSW_UNCOMP_OPFILE, 2, PATH_CSW_UNCOMP_OPFILE, $skip, $xs3, $cfg);
  $comp   = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_CSW_OPFILE, 2, PATH_CSW_OPFILE, $skip, $xs3, $cfg);
  print_report_update_successes ($st, strlen($comp)<strlen($nocomp), "csz<usz", "csz>=usz", "UNZCSW: compressed size < uncompressed size", $grp, $skip);


  $d_save = "b 4000\nbx 0\n".
              "paste ?&4000=&12|M*TAPE|M10DEF PROCWAIT(A):TIME=0:REPEAT UNTIL TIME>A|M".
                     "20ENDPROC|MPROCWAIT(100)|MSAVE\"A\"|M|MPROCWAIT(100)|MSAVE\"B\"|M|MCALL &4000|M\n".
              "c\n\n";
  $u = run_without_testing ($st, $d_save, "", "-tape112 -record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $a = array();
  $e = parse_uef ($u, $a);
  if (E_OK == $e) {
    // the rule is that data must never be followed immediately by silence
    for ($i=0; $i < (count($a)-1); $i++) {
      $type1 = $a[$i]['type'];
      $type2 = $a[$i+1]['type'];
      if (($type1 == 0x100) && (($type2 == 0x112) || ($type2 == 0x116))) {
        $e = E_SILENCE_AFTER_DATA;
        break;
      }
    }
  }
  print_report_update_successes ($st, $e==E_OK, "no D->S", "D->S", "silence immediately follows data", $grp, $skip);

  
  $a = array();
  $d = "b 4000\nbx 0\n".
       "paste ?&4000=&12|MTIME=0|M*MOTOR 1|MREPEAT UNTIL TIME>3000|M*MOTOR 0|MCALL &4000|M\nc\n\n"; // 30s: long enough to force two chunk &112s
  $u = run_without_testing ($st, $d, "", "-tape112 -record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs3, $cfg);
  $e = parse_uef ($u, $a);
  for ($i=0, $count112=0, $count116=0; (E_OK == $e) && ($i < count($a)); $i++) {
    if ($a[$i]['type']==0x112) {
      $count112++;
    } else if ($a[$i]['type']==0x116) {
      $count116++;
    }
  }
  print_report_update_successes ($st, ($e==E_OK)&&(2==$count112)&&(0==$count116), "112s OK", "112", "-tape112: multi required, 30s silence", $grp, $skip);

  $d = "b 4000\nbx 0\n".
       "paste ?&4000=&12|MTIME=0|M*MOTOR 1|MREPEAT UNTIL TIME>500|M*MOTOR 0|MCALL &4000|M\nc\n\n";
  $u = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $e = parse_uef ($u, $a);
  for ($i=0, $count112=0, $count116=0; (E_OK == $e) && ($i < count($a)); $i++) {
    if ($a[$i]['type']==0x112) {
      $count112++;
    } else if ($a[$i]['type']==0x116) {
      $count116++;
    }
  }
  print_report_update_successes ($st, ($e==E_OK)&&(0==$count112)&&(1==$count116), "116not112", "112not116", "confirm 116 not 112 without -tape112", $grp, $skip);


  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_TAPESAVE.$sep;
  $d = "b 4000\nbx 0\npaste ?&4000=&12|MCH.\"75\"|M|MCALL &4000|M\nc\n\n\n";
  // TOHv4.1: need longer $xs3 timeout value instead of $xs2 for 75 baud case
  $u = run_without_testing ($st, $d, "", "-disc ".$p."weirdbaud.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs3, $cfg);
  $e = tibet_tonechar_run_len_check($u, 16); // 16 1200ths (32 TIBET tonechars)
  print_report_update_successes ($st, ($e==E_OK), "RL16", "RL bad", "run lengths @ 75 baud", $grp, $skip);

  $d = "b 4000\nbx 0\npaste ?&4000=&12|MCH.\"150\"|M|MCALL &4000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."weirdbaud.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $e = tibet_tonechar_run_len_check($u, 8); // 8 1200ths (16 TIBET tonechars)
  print_report_update_successes ($st, ($e==E_OK), "RL8", "RL bad", "run lengths @ 150 baud", $grp, $skip);

  $d = "b 4000\nbx 0\npaste ?&4000=&12|MCH.\"300\"|M|MCALL &4000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."weirdbaud.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $e = tibet_tonechar_run_len_check($u, 4); // 4 1200ths (8 TIBET tonechars)
  print_report_update_successes ($st, ($e==E_OK), "RL4", "RL bad", "run lengths @ 300 baud", $grp, $skip);
  
  $d = "b 4000\nbx 0\npaste ?&4000=&12|MCH.\"600\"|M|M|MCALL &4000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."weirdbaud.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $e = tibet_tonechar_run_len_check($u, 2); // 2 1200ths (4 TIBET tonechars)
  print_report_update_successes ($st, ($e==E_OK), "RL2", "RL bad", "run lengths @ 600 baud", $grp, $skip);

  // save_framings stuff
  $d = "b 6000\nbx 0\npaste ?&6000=&12|M*8E1|MCALL &6000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."save_framings.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $g = strtr($u, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "data--..--..--..--..----..----..--..--..--..--..--------------------..--................--..--..--------....--....end"));
                             //     --..--..--..--..--  ..----..--..--..--..  ..------------------..--..................--..--------....--..

  print_report_update_successes($st, $ok, "ok", "fail", "Exotic TX framings: 8E1 (thx. vanekp)", $grp, $skip); //, $xs2);

  $d = "b 6000\nbx 0\npaste ?&6000=&12|M*8O1|MCALL &6000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."save_framings.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $g = strtr($u, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "data--..--..--..--..--....----..--..--..--......------------------....--....................--..--------....----..end"));
  print_report_update_successes($st, $ok, "ok", "fail", "Exotic TX framings: 8O1 (thx. vanekp)", $grp, $skip); //, $xs2);

  $d = "b 6000\nbx 0\npaste ?&6000=&12|M*7E2|MCALL &6000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."save_framings.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $g = strtr($u, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "data--..--..--..--..--....----..--..--..--......------------------....--....................--..--------..........end"));
  print_report_update_successes($st, $ok, "ok", "fail", "Exotic TX framings: 7E2", $grp, $skip); //, $xs2);

  $d = "b 6000\nbx 0\npaste ?&6000=&12|M*7O2|MCALL &6000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."save_framings.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $g = strtr($u, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "data--..--..--..--........----..--..--..----....----------------......--..............--....--..--------....--....end"));
  print_report_update_successes($st, $ok, "ok", "fail", "Exotic TX framings: 7O2", $grp, $skip); //, $xs2);

  // same as previous test, but to UEF this time ...
  $d = "b 6000\nbx 0\npaste ?&6000=&12|M*7O2|MCALL &6000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."save_framings.ssd -record -tapesave ".PATH_UEF_OPFILE, 2, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $chunks=array();
  $e = parse_uef ($u, $chunks);
  if ((E_OK == $e) && (count($chunks) != 5)) { $e = E_UEF_MAGIC; } // just borrow E_UEF_MAGIC
  $h=0;
  if (E_OK == $e) {
      // expect this chunk sequence:
      if (  0x0==$chunks[0]['type']) { $h++; } // origin
      if (0x100==$chunks[1]['type']) { $h++; } // dummy byte
      if (0x110==$chunks[2]['type']) { $h++; } // leader
      if (0x104==$chunks[3]['type']) { $h++; } // defined format block
      if (0x110==$chunks[4]['type']) { $h++; } // leader
      if ($h != 5) { $e = E_UEF_MAGIC; }  // just borrow E_UEF_MAGIC
  }
  print_report_update_successes($st, (E_OK==$e), "5 good", "$h good", "Exotic TX framings: 7O2 (UEF, check chunks)", $grp, $skip); //, $xs2);

  // chunk &104 sequence:
  //   07             : bits per packet
  //   4f             : 'O': parity code
  //   02             : 2 stop bits
  //   55 AA 00 FF 61 : data sequence; after 7-bit filtering, it will become 55 2A 00 7F 61
  print_report_update_successes($st, isset($chunks[3])&&$chunks[3]['data']=="\x07\x4f\x02\x55\x2a\x00\x7f\x61", "match", "no match", "Exotic TX framings: 7O2 (UEF, 7-bit data)", $grp, $skip);

  $d = "b 6000\nbx 0\npaste ?&6000=&12|M*8N2|MCALL &6000|M\nc\n\n\n";
  $u = run_without_testing ($st, $d, "", "-disc ".$p."save_framings.ssd -record -tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $g = strtr($u, array("\n"=>"")); // strip \n
  $ok = (FALSE !== @strpos($g, "data--..--..--..--..--....----..--..--..--......------------------....--....................--..--------....--....end"));
  print_report_update_successes($st, $ok, "ok", "fail", "Exotic TX framings: 8N2", $grp, $skip); //, $xs2);

  $d = "b 4000\nb 5000\nb 6000\nb fa18\nb fabb\nbx 0\nbx 1\nbx 3\nbx 4\n".
       "paste ?&6000=&60|M*TAPE|M10 CALL &5000|MSAVE \"5K\"|M|M*MOTOR 1|MTIME=0|MREPEAT UNTIL TIME>100|M*MOTOR 0|MCALL &6000|M\nc\n".
       "paste ?&4000=&12|M?&5000=&12|MCH.\"\"|M\ntaperec off\ntapeseek 50%\n\nc\n\n";
  // these tapes have a "CALL &4000" program on them already
  run ($st, $d, $p."010__CALL4000_append_then_seek.csw",     "-record" , $grp, X_BP1, 3, "append, then seek: CSW",    $skip, array(), $xs2, $cfg);
  run ($st, $d, $p."011__CALL4000_append_then_seek.tibetz",  "-record" , $grp, X_BP1, 3, "append, then seek: TIBETZ", $skip, array(), $xs2, $cfg);
  run ($st, $d, $p."012__CALL4000_append_then_seek.uef",     "-record" , $grp, X_BP1, 3, "append, then seek: UEF",    $skip, array(), $xs2, $cfg);

  // die();

  // The family dog.
  $a = array();
  $d = "b 6000\n".
        "bx 0\n".
        "b 7000\n".
        "paste ?&6000=&60|M?&7000=&60|M*TAPE|M10?&4000=&60|M20CALL&4000|MSAVE\"4000\"|M|MCALL &7000|M\n".
        "c\n".
        "tapeeject\n".
        "taperec on\n".
        "paste NEW|M10?&5000=&60|M20CALL&5000|MSAVE\"5000\"|M|MCALL &6000|M\n".
        "c\n";
  $g = run_without_testing ($st, $d, "", "-record -tapesave ".PATH_UEF_OPFILE, 0, PATH_UEF_OPFILE, $skip, $xs2, $cfg);
  $d = "b 4000\n".
       "b 5000\n".
       "bx 0\n".
       "bx 1\n".
       "paste *TAPE|MCH.\"\"|M\n".
       "c\n";
  run ($st, $d, PATH_UEF_OPFILE,  "", $grp, X_BP1, 3, "Eject, then record", $skip, array(), $xs, $cfg);

  // * F821 breakpoint will deactivate REC after the filename has been written
  // * and before load, exec, block number etc. The final two characters of the
  // * filename ('9' and '\0') go missing in the TX pipeline.

  $d = "b f821\nb 5000\nbx 1\ntaperec on\npaste *TAPE|MSAVE\"0123456789\"|M|M\nc\ntaperec off\npaste CALL &5000|M\nc\n";
  $g = run_without_testing ($st, $d, "", "-tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  print_report_update_successes  ($st, ($g==get_aborted_block_tibet_output()), "match", "no match", "REC off during block SAVE (TIBET)", $grp, $skip);

  $g = run_without_testing ($st, $d, "", "-tapesave ".PATH_UEF_UNCOMP_OPFILE, 2, PATH_UEF_UNCOMP_OPFILE, $skip, $xs2, $cfg);
  $chunkz = array();
  $e = parse_uef($g, $chunkz);

  // make sure $chunkz[3] contains the expected data
  // UEF is exactly the same as TIBET; stops after 2/4 bytes of next file address
  $win = (    isset($chunkz[5])
           && ($chunkz[5]["type"] == 0x100)
           && ($chunkz[5]["data"] == "*012345678"));
  print_report_update_successes  ($st, $win, "chnkmatch", "no match", "REC off during block SAVE (UEF)", $grp, $skip);



  //enable REC during block save. This time do it immediately before the
  //data CRC is written, by breakpointing f852
  $d = "b f852\nb 5000\nbx 1\npaste *TAPE|MSAVE\"0123456789\"|M|M\nc\ntaperec on\npaste CALL &5000|M\nc\n";
  $g = run_without_testing ($st, $d, "", "-tapesave ".PATH_TIBET_OPFILE, 2, PATH_TIBET_OPFILE, $skip, $xs2, $cfg);
  $foo = "tibet 0.5\n\n/baud 1200\n/framing 8N1\ndata\n--..--....--------..\n--..................\n--------..--....--..\n------....--..--....\nend\n\nleader 12712\n\n";
  print_report_update_successes  ($st, ($g==$foo), "half-DCRC", "no match", "REC during SAVE (TIBET)", $grp, $skip);

  $d = "b f852\nb 5000\nbx 1\npaste *TAPE|MSAVE\"0123456789\"|M|M\nc\ntaperec on\npaste CALL &5000|M\nc\n";
  $g = run_without_testing ($st, $d, "", "-tapesave ".PATH_UEF_UNCOMP_OPFILE, 2, PATH_UEF_UNCOMP_OPFILE, $skip, $xs2, $cfg);
  $chunkz = array();
  $e = parse_uef($g, $chunkz);
// print_r($chunkz); die();
  $win = (    isset($chunkz[1])
           && (count($chunkz)==3)
           && ($chunkz[1]["type"] == 0x100)
           && ($chunkz[1]["data"] == "\x0d\xff\x68\xac"));
  print_report_update_successes  ($st, $win, "0dff68ac", "no match", "REC during SAVE (UEF)", $grp, $skip);

  $d="b 5000\nb 6000\nbx 1\n".
     "paste ?&5000=&60|M?&6000=&12|M10DEF PROCWAIT(A)|M20TIME=0|M30REPEAT UNTIL TIME>A|M40ENDPROC|M*TAPE|M*MOTOR1|M".
     "PROCWAIT(100)|M*SAVE A A000 A100|M|M*MOTOR1|MPROCWAIT(500)|M*MOTOR0|M".
     "*SAVE B A200 A300|M|M*MOTOR1|MPROCWAIT(100)|M*MOTOR0|M".
     "CALL &5000|M".
     "\nc\nbclear 0\ntaperec off\npaste PROCWAIT(100)|MCALL &6000|M\nc\nq\n". //paste CALL &6000|M\nc\n";
     "";
  run ($st, $d, "", "-record", $grp, X_BP1, 3, "Appending to tapectrl stripes (\"UEF\", 1200)", $skip, array(), $xs3, $cfg);

  $d="b 5000\nb 6000\nbx 1\n".
     "paste ?&5000=&60|M?&6000=&12|M10DEF PROCWAIT(A)|M20TIME=0|M30REPEAT UNTIL TIME>A|M40ENDPROC|M*TAPE3|M*MOTOR1|M".
     "PROCWAIT(100)|M*SAVE A A000 A100|M|M*MOTOR1|MPROCWAIT(500)|M*MOTOR0|M".
     "*SAVE B A200 A300|M|M*MOTOR1|MPROCWAIT(100)|M*MOTOR0|M".
     "CALL &5000|M".
     "\nc\nbclear 0\ntaperec off\npaste PROCWAIT(100)|MCALL &6000|M\nc\nq\n". //paste CALL &6000|M\nc\n";
     "";
  run ($st, $d, "", "-record", $grp, X_BP1, 3, "Appending to tapectrl stripes (\"UEF\", 300)", $skip, array(), $xs3, $cfg);

  // again, for CSW: start with blank.csw in order to force use of CSW file representation (not default UEF)
  $d= "b 5000\nb 6000\nbx 1\n".
      "paste ?&5000=&60|M?&6000=&12|M10DEF PROCWAIT(A)|M20TIME=0|M30REPEAT UNTIL TIME>A|M40ENDPROC|M*TAPE|M*MOTOR1|M".
      "PROCWAIT(100)|M*SAVE A A000 A100|M|M*MOTOR1|MPROCWAIT(500)|M*MOTOR0|M".
      "*SAVE B A200 A300|M|M*MOTOR1|MPROCWAIT(100)|M*MOTOR0|M".
      "CALL &5000|M".
      "\nc\nbclear 0\ntaperec off\npaste PROCWAIT(100)|MCALL &6000|M\nc\nq\n". //paste CALL &6000|M\nc\n";
      "";
  run ($st, $d, $p."blank.csw", "-record", $grp, X_BP1, 3, "Appending to tapectrl stripes (CSW, 1200)", $skip, array(), $xs3, $cfg);

  // again, for TIBET: start with blank.tibet
  $d= "b 5000\nb 6000\nbx 1\n".
      "paste ?&5000=&60|M?&6000=&12|M10DEF PROCWAIT(A)|M20TIME=0|M30REPEAT UNTIL TIME>A|M40ENDPROC|M*TAPE|M*MOTOR1|M".
      "PROCWAIT(100)|M*SAVE A A000 A100|M|M*MOTOR1|MPROCWAIT(500)|M*MOTOR0|M".
      "*SAVE B A200 A300|M|M*MOTOR1|MPROCWAIT(100)|M*MOTOR0|M".
      "CALL &5000|M".
      "\nc\nbclear 0\ntaperec off\npaste PROCWAIT(100)|MCALL &6000|M\nc\nq\n". //paste CALL &6000|M\nc\n";
      "";
  run ($st, $d, $p."blank.tibet", "-record", $grp, X_BP1, 3, "Appending to tapectrl stripes (TIBET, 1200)", $skip, array(), $xs3, $cfg);

  $d="taperec on\n".
     "b 5000\n".
     "b 5010\n".
     "bx 0\n".
     "paste ?&5000=&12|M?&5010=&60|M*TAPE3|M*SAVE 300 A000 A200|M|MNEW|M10 CALL &5000|M*TAPE|MSAVE\"5000\"|M|MCALL &5010|M\n".
     "c\n".
     "taperec off\n".
     "tapeseek 22\n".
     "paste CH.\"\"|M\n".
     "c\n";
  run ($st, $d, "", "", $grp, X_BP0, 3, "Save 300 baud then seek into it", $skip, array(), $xs3, $cfg);



  // Test REC spam. Not sure how failure should be defined here. Mainly just
  // want to make sure it doesn't crash or throw TAPE_E_BUG. Do a seek too.
  // Set up lots of read breakpoints within page A0; use them to toggle REC
  // as the block is written.
  $d="";
  // $d.="b 5000\n"; // ultimately use this to exit, needs to be defined first
  for ($i=1;$i<15;$i++) {
    $d .= sprintf("breakr F:a0%x0\n", $i);
  }
  // $d = "paste ?&5000=&60\nbreak 5000\n";
  // $d .= "paste CALL &5000|M\nc\n";
  $d .= "paste *TAPE|M*SAVE A A000 A100|M|M\nc\n";
  for ($i=1,$j=true;$i<15;$i++,$j=!$j) {
    // will need to 'continue' 13 times as the watchpoints hit. each time toggle REC.
    $d .= sprintf("taperec %s\n", $j?"on":"off", $i);
    if ($i==14) { $d.="tapeseek 50%\n"; }
    if ($i<15) { $d.="c\n"; }
  }
  // $d .= "bx 0\npaste CALL &5000|M\nc\n";

  // UEF (=>don't load a tape)
  run ($st, $d, "", "-record -tapesave ".PATH_UEF_OPFILE, $grp, X_EOF, 3, "Rapid REC toggle during block SAVE (UEF)", $skip, array(), $xs2, $cfg);
// die();
  // TIBET (begin with empty TIBET file; force TIBET mode)
  run ($st, $d, $p."blank.tibet", "-record -tapesave ".PATH_TIBET_OPFILE, $grp, X_EOF, 3, "Rapid REC toggle during block SAVE (TIBET)", $skip, array(), $xs2, $cfg);
  // CSW (begin with empty CSW file; force TIBET mode)
  run ($st, $d, $p."blank.csw", "-record -tapesave ".PATH_CSW_OPFILE, $grp, X_EOF, 3, "Rapid REC toggle during block SAVE (CSW)", $skip, array(), $xs2, $cfg);

 // die();

  // ==================== END TAPESAVE SET ========================




  // =============== BEGIN BEEBJIT PROTECTED SET ==================
  
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_BEEBJIT_SET.$sep;
  $skip = ! $st->run_tape_beebjit_set;
  $grp = "prot-beebjit";

  // Tests below mostly set breakpoints at &FA18, &FABB -- parts of the MOS CFS error handling routine.
  // Now use "-tapetest 3" -- quit on EOF and error, but don't autocat on start.

  // Arcadians: expect breakpoint hit at &4AAA.
  // Failure modes are CFS errors (FABB, FA18), and EOF.
  $d = "b 4AAA\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nc\n\n\n";
  run ($st, $d, $p."Arcadians2_AcornSoft_QB2_TOHv33-PHANTOMBLOCK.csw",           "", $grp, X_BP0, 3, "Arcadians", $skip, array(), $xs3, $cfg);
  
  // Arcadians with phantom block mitigation disabled (should fail at fa18, because of rogue CFS error).
  $d = "b 4AAA\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nc\n\n\n";
  run ($st, $d, $p."Arcadians2_AcornSoft_QB2_TOHv33-PHANTOMBLOCK.csw", "-tapenopbp", $grp, X_BP1, 3, "Arcadians w/o phantom block mitigation", $skip, array(), $xs3, $cfg);

  // Atic-Atac: expect breakpoint hit at &28e3.
  // Failure modes are error routine at &6BA, CFS error and EOF
  $d = "b 28e3\nb 6ba\nb fa18\nb fabb\npaste *TAPE|M*RUN|M\nbx 0\nbx 1\nbx 2\nbx 3\nc\n";
  run ($st, $d, $p."AticAtac_RUN_B.hq.uef",                                              "", $grp, X_BP0, 3, "Atic Atac", $skip, array(), $xs3, $cfg);
  
  // Caveman Capers: expect breakpoint hit at &164D
  // Failure modes are CFS error and EOF.
  $d = "b 164d\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nc\nc\nc\n"; // TOHv4 adds "c\nc\n"
  run ($st, $d, $p."CavemanCapers_B.hq.uef",                                            "", $grp, X_BP0, 3, "Caveman Capers", $skip, array(), $xs3, $cfg);
  
  // Dune Rid0r
  // This one was annoying; it does have an error handler for the custom loader,
  // but I had trouble intercepting it in a reliable way that didn't false-positive
  // in other situations. There is a failure error message ("rewind tape" etc.) at
  // &524 in case anyone else wants to try.
  // So we just rely on EOF catching the end of the tape if the load doesn't succeed
  $d = "b 724c\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nc\nc\nc\n"; // TOHv4: added "c/nc/n"
  run ($st, $d, $p."DuneRider_MicroPower.uef",                                          "", $grp, X_BP0, 3, "Dune Rider", $skip, array(), $xs3, $cfg);
  
  // Way of the Exploding Fist: win at 2671; custom failure handling at 0FD7; CFS at fa18/fabb
  $d = "b 2671\nb 0fd7\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nbx 3\nc\n";
  run ($st, $d, $p."ExplodingFist-QB2.tibetz",                                          "", $grp, X_BP0, 3, "Way of the Exploding Fist", $skip, array(), $xs3, $cfg);
  
  // Fortress
  // Awkward, need to get a SPACE into the keyboard buffer to continue loading.
  // Mercifully the loader doesn't empty the keyboard buffer so we can just paste it in at start.
  // However, the paste command won't let you place a space as the first character, so we put
  // a dummy O in at the start; seems like multiple spaces are also required, so we do that too.
  // After all that, win at 12e3; fail state is EOF, CFS errors, error handlers at &6209 and &70a7
  // for framing and parity errors
  $d = "b 12e3\nb 6209\nb 70a7\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|MO   \nbx 0\nbx 1\nbx 2\nbx 3\nbx 4\nc\nc\n\n";
  run ($st, $d, $p."Fortress-Pace.csw",                                                 "", $grp, X_BP0, 3, "Fortress", $skip, array(), $xs3, $cfg);
  
  // Frak!: breakpoint at d03, failure on EOF or CFSe
  $d = "b d03\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nc\n";
  run ($st, $d, $p."Frak_B.uef",                                                        "", $grp, X_BP0, 3, "Frak", $skip, array(), $xs3, $cfg);
  
  // Jet Set Willy, 425 or EOF/CFSe
  $d = "b 425\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nc\n";
  run ($st, $d, $p."JetSetWilly_Tynesoft.csw",                                          "", $grp, X_BP0, 3, "Jet Set Willy", $skip, array(), $xs3, $cfg);
  
  // Joust: success at 2623, failure at d9cd (reset vector contents), EOF or CFS error
  $d = "b 2623\nb d9cd\nb fa18\nb fabb\npaste *TAPE|MCH.\"\"|M\nbx 0\nbx 1\nbx 2\nbx 3\nc\n";
  run ($st, $d, $p."Joust_RUN_B.hq.uef",                                                "", $grp, X_BP0, 3, "Joust", $skip, array(), $xs3, $cfg);
  
  // Missile Control: This one is annoying. Flushes buffers, so we cannot insert keystrokes before load ...
  // Place *non-fatal* breakpoint 2 at E1CB (which is the MOS flush buffer epilogue). Fatal CFSe breakpoints
  // 1 and 2 go in as normal. Once non-fatal at e1cb is hit, we can delete that breakpoint and insert keystrokes.
  // Next set up a non-fatal breakpoint at 3400 and wait for that to be hit. Once that happens, set up another
  // THEN insert keystrokes into buffer, delete the breakpoint. bp 0 is fatal in-game breakpoint at &23c9.
  //$d = "b fa18\nb fabb\nb e1cb\nbx 0\nbx 1\npaste *TAPE|MCH.\"\"|M\nc\nbclear 2\npaste O |M\nbreak 3400\nc\nbx 4\nb 23c9\nbx 5\nc\n";
  $d = "b 23c9\nb fa18\nb fabb\nb e1cb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCH.\"\"|M\nc\nbclear 3\npaste O |M\nb 3400\nc\nc\nc\nc\n";
  run ($st, $d, $p."MissileControl_Gemini.hq.uef",                                     "", $grp, X_BP0, 3, "Missile Control", $skip, array(), $xs3, $cfg);
  
  // Nightshade: success at 5ee0, failure on c0b, or EOF or CFS error
  $d = "b 5ee0\nb fa18\nb fabb\nb c0b\nbx 0\nbx 1\nbx 2\nbx 3\npaste *TAPE|MCH.\"\"|M\nc\n";
  run ($st, $d, $p."Nightshade.uef",                                                   "", $grp, X_BP0, 3, "Nightshade", $skip, array(), $xs3, $cfg);
  
  // Pro Boxing Simulator
  // success at 5460, BUT instead we'll use *0895* which is the GAME OVER wait routine.
  // v3.3: Failed protection will hang the emulator, so the xs3 time limit is essential here.
  // v4:   Added "b 6065" to catch set_twenty_bytes subroutine, which corrupts memory on failure
  // v4.1: now use 26ff to catch 'consider_scoring_for_move' as success
  //$d = "b 0895\nb fa18\nb fabb\nb 6065\nbx 0\nbx 1\nbx 2\nbx 3\npaste *TAPE|MCH.\"\"|M\nc\nc\n";
  $d = "b 26ff\nb fa18\nb fabb\nb 6065\nbx 0\nbx 1\nbx 2\nbx 3\npaste *TAPE|MCH.\"\"|M\nc\n"; // v4.1 edition
  run ($st, $d, $p."ProBoxingSimulator_B.hq.uef",                                      "", $grp, X_BP0, 3, "Pro Boxing Simulator", $skip, array(), $xs3, $cfg);
  
  // Star Drifter
  // success FB8
  // TODO: again, trying to pin down a failure breakpoint is annoying, so rely on EOF
  $d = "b fb8\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCH.\"\"|M\nc\n";
  run ($st, $d, $p."StarDrifter_B.hq.uef",                                             "", $grp, X_BP0, 3, "Star Drifter", $skip, array(), $xs3, $cfg);
  
  // Starquake (TIBETZ)
  // break at 1301 to trap key selection screen (then clear it once hit),
  // then we patch out the key definition routines and finally paste 'Y' to continue loading.
  // success is at 7f2f; TODO: failure is again EOF or CFSe based
  $d = "b 7f2f\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nb 1301\npaste *TAPE|MCH.\"\"|M\nc\nbclear 3\nwritem 12fe ea\nwritem 12ff ea\nwritem 1300 ea\nwritem 1303 ea\nwritem 1304 ea\npaste y\nc\n";
  run ($st, $d, $p."Starquake-QB2.tibetz",                                             "", $grp, X_BP0, 3, "Starquake (TIBETZ)", $skip, array(), $xs3, $cfg);
  
  // Swarm
  // Success 40c7; failure is just MOS stuff
  // (this has one block at 300 baud)
  $d = "b 40c7\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCH.\"\"|M\nc\nc\nc\n"; // TOHv4 adds "c\nc\n"
  run ($st, $d, $p."Swarm-Computer_Concepts.csw",                                     "", $grp, X_BP0, 3, "Swarm", $skip, array(), $xs3, $cfg);

  // The Music System
  // - success (main menu) 554b (was 56a2)
  // - failure is tricky to detect :( EOF sometimes can get you there, but
  //   some failure modes can just dump you back to the BASIC interpreter, or
  //   get you a HLT instruction ... hmph
  // - Set a breakpoint at &12BB in order to break into routine before the copy protection that is failing in v4
  //   - this will break you into the loader immediately after chunk #68, type &100
  //     - successful execution will do three more serial ULA reg writes after this point
  //     - failed execution won't do this
  //   - loader code begins at &1151?
  //   - entrypoint:

  // 1100: 4C FC 11    JMP 11FC     00 E4 00 FD     ZC 1100
  // 11FC: 20 82 14    JSR 1482     00 E4 00 FD     ZC 11FC
  // 1482: A9 8B       LDA #8B      00 E4 00 FB     ZC 1482

  // was 56a2
  // NOTE the MOS CFS error fa18 trap has been removed here. Test kept failing with it for some reason
  $d = "b 554b\nb fabb\nbx 0\nbx 1\npaste *TAPE|MCH.\"\"|M\nc\nc\nc\n"; // TOHv4: "c\nc\n"
  run ($st, $d, $p."TheMusicSystem_IslandLogic_Tape1Side1.uef",                     "", $grp, X_BP0, 3, "The Music System, load+run (to menu)", $skip, array(), $xs3, $cfg);
  
  // Uridium ...
  // after CHAIN"", stuff "1" into keyboard buffer to select keys at menu
  // (mercifully it doesn't flush the buffer this time)
  // success at 1748
  $d = "b 1748\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCH.\"\"|M1\nc\n";
  run ($st, $d, $p."Uridium.csw",                                     "", $grp, X_BP0, 3, "Uridium", $skip, array(), $xs3, $cfg);
  
  // Psycastria
  // 6105 and ce2 are the OS resets in the loader if checksum fails
  // 7ae is "Loading error -- rewind tape to start"
  // 6f00 is win condition
  $d = "b 6f00\nb fa18\nb fabb\nb 6105\nb ce2\nb 7ae\nbx 0\nbx 1\nbx 2\nbx 3\nbx 4\nbx 5\npaste *TAPE|MCH.\"\"|M\nc\n";
  run ($st, $d, $p."psycastria.tibetz",                                     "", $grp, X_BP0, 3, "Psycastria", $skip, array(), $xs3, $cfg);
  
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_MAKEUEF_PARITY_FIX.$sep;
  $skip = ! $st->run_tape_makeuef_parity_fix;
  $grp = "makeuef";
  // stuff a load of spaces into kb buffer so that it skips instructions
  // rely on EOF for failure
  // both tapes should load, even though they have opposite parities
  $d = "b 4a20\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCH.\"\"|M             \nc\n";
  run ($st, $d, $p."Estra_B_MAKEUEF1.9.hq.uef",                          "", $grp, X_BP0, 3, "Estra (MakeUEF 1.9, with parity bug)", $skip, array(), $xs3, $cfg);
  run ($st, $d, $p."Estra_Firebird_Side1Instructions_MAKEUEF2.4.hq.uef", "", $grp, X_BP0, 3, "Estra (MakeUEF 2.4, bug fixed)",       $skip, array(), $xs3, $cfg);
  
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_OTHER_PROTECTED.$sep;
  $skip = ! $st->run_tape_other_protected;
  $grp = "prot-x";
  
  // success if 4aca, fail on CFS fail or EOT
  $d = "b 4aca\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|M\nc\n";
  run ($st, $d, $p."SabreWulf_RUN_B.uef",                          "", $grp, X_BP0, 3, "Sabre Wulf", $skip, array(), $xs3, $cfg);
  // success if 4aca, fail on CFS fail or EOT; also "Load Error" code at &4BB
  $d = "b AD0\nb fa18\nb fabb\nb 4BB\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|M\nc\n";
  run ($st, $d, $p."Alien8_RUN_B.hq.uef",                          "", $grp, X_BP0, 3, "Alien8", $skip, array(), $xs3, $cfg);
  // Chip Buster, win at 4279 (text printing routine)
  $d = "b 4279\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCHAIN\"\"|M\nc\n";
  run ($st, $d, $p."ChipBuster_B.hq.uef",                          "", $grp, X_BP0, 3, "Chip Buster", $skip, array(), $xs3, $cfg);
  // JetPac, success at &5900
  // ** FIXME **: looks like standard MOS loading + post-decrypt, pointless? maybe just omit this test?
  $d = "b 5900\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|M\nc\n";
  run ($st, $d, $p."JetPac_RUN_B.uef",                          "", $grp, X_BP0, 3, "JetPac", $skip, array(), $xs3, $cfg);
  // The Hacker
  // winning at &400, fail on custom tape error routine, VDU7 for this is at 5612
  $d = "b 400\nb 5612\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\npaste *TAPE|MCHAIN\"\"|M\nc\n";
  run ($st, $d, $p."TheHacker_B.hq.uef",                          "", $grp, X_BP0, 3, "The Hacker", $skip, array(), $xs3, $cfg);
  // Knight Lore
  // 137B to win, "Tape Load Error" string is at 4AE, fail at 49b
  $d = "b 137b\nb fa18\nb fabb\nb 49b\nbx 0\nbx 1\nbx 2\nbx 3\npaste *TAPE|M*RUN|M\nc\n";
  run ($st, $d, $p."KnightLore_RUN_B.hq.uef",                          "", $grp, X_BP0, 3, "Knight Lore", $skip, array(), $xs3, $cfg);
  // Video's Revenge (chunk &104)
  // win at db7, fail on CFSe or tape EOF
  $d = "b db7\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCHAIN\"\"|M\nc\n";
  run ($st, $d, $p."videos-revenge-104.uef",                          "", $grp, X_BP0, 3, "Video's Revenge as chunk &104", $skip, array(), $xs3, $cfg);
  // Haunted Abbey leader/silence pulse protected loader
  // paste N into kb buffer to pass the "want instructions?" prompt
  $d="b 69F6\nb 5b4\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\nbx 3\npaste *TAPE|MCH.\"\"|MN\nc\n";
  run ($st, $d, $p."Haunted_Abbey_AnF_B_Tape_side-lab.hq.uef",       "", $grp, X_BP0, 3, "Haunted Abbey (thx. Sazhen86)", $skip, array(), $xs3, $cfg);
  


//$p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_OTHER_PROTECTED.$sep;
//$skip = ! $st->run_tape_other_protected;
//$grp = "prot-x";

  // Castle Quest
  // win at b56, fail on CFSe or EOF
  $d="b b56\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|MCH.\"\"|Mn\nc\n";
  run ($st, $d, $p."Castle_Quest_QB2.tibetz",  "", $grp, X_BP0, 3, "Castle Quest (thx. Sazhen86)", $skip, array(), $xs3, $cfg);
  
  $p = PATH_TO_TAPE_TESTS.$sep.PATH_SUBSET_ULTRON_SURPRISE.$sep;
  $skip = ! $st->run_tape_ultron_surprise;
  $grp = "ultron";

  // Viper's Ultron switches between 300 and 1200 baud without warning, so it is useful
  // for testing multiple baud-related things
  //if ($run_tape_ultron_surprise) {
  // 1. TIBET, hinted with /baud directives (should load)
  // (note that we have to insert N into the kb buffer to get past "music while loading? Y/N")
  // 49e8 will break after successful load:
  $d = "b 49E8\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|MN\nc\n";
  run ($st, $d, $p."ultron-baud-hinted.tibetz",                          "", $grp, X_BP0, 3, "Hinted TIBET (should load)", $skip, array(), $xs3, $cfg);
  // 2. TIBET, unhinted (this should also load):
  $d = "b 49E8\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|MN\nc\n";
  run ($st, $d, $p."ultron-unhinted.tibetz",                          "", $grp, X_BP0, 3, "Unhinted TIBET (should load)", $skip, array(), $xs3, $cfg);
  // 3. CSW (should load):
  $d = "b 49E8\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|MN\nc\n";
  run ($st, $d, $p."ultron.csw",                          "", $grp, X_BP0, 3, "CSW (should load)", $skip, array(), $xs3, $cfg);
  // 4. UEF chunk 114 (that's the security cycles chunk) (should load):
  $d = "b 49E8\nb fa18\nb fabb\nbx 0\nbx 1\nbx 2\npaste *TAPE|M*RUN|MN\nc\n";
  run ($st, $d, $p."ultron114.uef",                          "", $grp, X_BP0, 3, "UEF-&114 (should load)", $skip, array(), $xs3, $cfg);
  // 5. Unhinted UEF-100; this should FAIL, as b-em should take bytes from &100-chunks,
  // and format according to whatever baud rate is currently set by &107, sending the
  // result to the ACIA; so it should fail here, even though its data is correct
  $d = "b fa18\nb fabb\nbx 0\nbx 1\npaste *TAPE|M*RUN|MN\nc\n";
  run ($st, $d, $p."ultron100-no-chunk107-for-baud-SHOULD-FAIL.uef", "", $grp, X_EOF, 3, "Unhinted UEF-&100 (should EOT)", $skip, array(), $xs3, $cfg);

  $elapsed = $st->getElapsed();

  $hours = (int) ($elapsed / 3600);
  $mins  = $elapsed % 3600;
  $secs  = $mins % 60;
  $mins  = (int) ($mins / 60);

  $elapsed_s = "";
  if ($hours > 0) {
    $elapsed_s.= $hours.":";
  }
  if (($hours > 0) || ($mins > 0)) {
    $elapsed_s.= sprintf("%02d",$mins).":";
  }
  if ($mins == 0) {
    $elapsed_s .= "$secs second".(($secs==1)?"":"s");
  } else {
    $elapsed_s .= sprintf("%02d",$secs);
  }

  print "\nT\nT    RESULTS: $st->successes / $st->testnum tests succeeded, $st->skips skipped; took $elapsed_s\n\n";

  die();
  
  // ====================================================
  //                         FIN
  // ====================================================

  
  function print_report_update_successes  (TestsState &$st,
                                           bool $result_to_report,
                                           string $msg_good,
                                           string $msg_bad,
                                           string $test_name,
                                           string $test_group_name,
                                           bool $skip) {
    $st->testnum++;
    $test_name = str_pad ($test_group_name.":".$test_name, TEST_NAME_STRING_PAD, " ");
    print "T";
    if ($skip) {
      print "    ".$test_name." ";
      $st->testnum--; // cancel test
      print "skip (-)\n";
    } else {
      print sprintf ("%3d", $st->testnum).":".$test_name." ";
      if ( ! $result_to_report ) {
        print "FAIL ".(($msg_bad!="") ? ("(".$msg_bad.")") : "")."\n";
      } else {
        print "ok   ".(($msg_good!="") ? ("(".$msg_good.")") : "")."\n";
        $st->successes++;
      }
    }
  }

  /*
  // returns decompressed data
  function test_gunzip (int &$tests_count,
                        int &$successes_count,
                        string $test_name,
                        string $test_group_name,
                        string $gz_data,
                        bool $expect_success) : string {
    //$tests_count++;
    $result = TRUE;
    //$test_name = str_pad ($test_name, TEST_NAME_STRING_PAD, " ");
    $ret = "";
    //print sprintf ("%3d", $tests_count)." ".$test_name." ";
    $gunz = gzdecode($gz_data);
    $ok = (FALSE !== $gunz);
    $result = ! ($expect_success XOR $ok);
    //if ( ! $result ) {
    //  print "FAIL (gunzip failed)\n";
    //} else {
    //  print "ok (gunzip succeeded)\n";
    //  $successes_count++;
    //}
    if ($ok) {
      $ret = $gunz;
    }
    print_report_update_successes  ($tests_count,
                                    $successes_count,
                                    $result,
                                    "gunzip succeeded",
                                    "gunzip failed",
                                    $test_name,
                                    $test_group_name,
                                    FALSE);
    return $ret;
  }
  */
  
  /*
  function wipe_inspect_dir_and_create_file (string $file) {
    global $path_sep;
    delete_dir_contents("_inspect");
    if (FALSE === file_put_contents("_inspect".$path_sep.$file, "")) {
      print "wipe_inspect_dir_and_create_file: file_put_contents() failed\n";
      die();
    }
  }
  */
  
  /*
  function check_file (int &$tests_count,
                       int &$successes_count,
                       string $test_name,
                       string $test_group_name,
                       string $fn,
                       bool $expected,
                       string &$file_contents, // out
                       $in_len) {
    //$tests_count++;
    $result = TRUE;
    //$test_name = str_pad ($test_name, TEST_NAME_STRING_PAD, " ");
    //print sprintf ("%3d", $tests_count)." ".$test_name." ";
    if (FALSE === ($x = @file_get_contents($fn))) {
      $x="";
    }
    $file_contents = $x;
    $found = (($len = strlen($x)) > 0);
    if ($found) {
      $msg = "file found";
    } else {
      $msg = "file not found";
    }
    $result = ! ($expected XOR $found);
    if ($expected && $found && (FALSE !== $in_len)) {
      // check length of inspect file is approx. similar to length
      // of source WAV file; BEWARE though: INSPECT FILE IS 8-BIT, so:
      $half_in_len = $in_len / 2;
      if (($len < ($half_in_len - 4000)) || ($len > ($half_in_len + 4000))) {
        $result = FALSE;
        $msg = "lengths: $len, $half_in_len";
      }
    }
    //if ( ! $result ) {
    //  print "FAIL ($msg)\n";
    //} else {
    //  print "ok ($msg)\n";
    //  $successes_count++;
   // }
    print_report_update_successes  ($tests_count,
                                    $successes_count,
                                    $result,
                                    $msg,
                                    $msg,
                                    $test_name,
                                    $test_group_name,
                                    FALSE);
    return $result;
  }
  */
  
  
  function create_some_file (string $fn) {
    if (FALSE === file_put_contents($fn, "hello")) {
      print "E: file_put_contents($fn) failed\n";
      die();
    }
  }
  
  // doesn't recurse, but we don't need that
  function delete_dir_contents (string $dir) {
    $files = scandir($dir);
    foreach ($files as $_=>$name) {
      if (($name == ".") || ($name == "..")) {
        continue;
      }
      if (FALSE === unlink($dir.PATH_SEP.$name)) {
        print "E: delete_dir_contents failed ($dir".PATH_SEP."$name)\n";
        die();
      }
    }
  }

  function delete_tmpfiles() {
    @unlink(PATH_DEBUGGER_EXEC_TMPFILE);
    @unlink(PATH_UEF_OPFILE);
    @unlink(PATH_CSW_OPFILE);
    @unlink(PATH_TIBET_OPFILE);
    @unlink(PATH_TIBETZ_OPFILE);
    @unlink(PATH_WAV_OPFILE);
    @unlink(PATH_UEF_UNCOMP_OPFILE); // 3.2
    @unlink(PATH_CSW_UNCOMP_OPFILE); // 3.2
    @unlink(PATH_SERIAL_OPFILE);     // 4.0
  }
  
  /*
  function tibet_legal_chars (int &$tests_count,
                              int &$successes_count,
                              string $test_name,
                              string $buf) {
    $result = TRUE;
    $len = strlen($buf);
    $badbyte="";
    for ($i=0; $i < $len; $i++) {
      $c = ord($buf[$i]);
      if ((($c < 0x20) || ($c > 0x7e)) && ($c != 0xa)) {
        $result = FALSE;
        $badbyte = sprintf("0x%x", ord($buf[$i]));
        break;
      }
    }
    print_report_update_successes  ($tests_count,
                                    $successes_count,
                                    $result,
                                    "",
                                    $badbyte,
                                    $test_name);
  }
  */
  
  /*
  function contains_string (int &$tests_count,
                            int &$successes_count,
                            string $test_name,
                            string $test_group_name,
                            string $haystack,
                            string $needle,
                            bool $expect_match) {
    $result = TRUE;
    $found = (FALSE !== (strpos ($haystack, $needle))); // x TRUE if needle found
    if ( $expect_match XOR $found ) {
      $result = FALSE;
    }
    if ($found) {
      $msg = "found";
    } else {
      $msg = "not found";
    }
    
    print_report_update_successes  ($tests_count,
                                    $successes_count,
                                    $result,
                                    $msg,
                                    $msg,
                                    $test_name,
                                    $test_group_name,
                                    FALSE);
    return $result;
  }
  */
  
  // FIXME? compare two files' contents -- useful here or not?
  /*
  function compare_contents  (int &$tests_count,
                              int &$successes_count,
                              string $test_name,
                              string $test_group_name,
                              string $c1,
                              string $c2,
                              bool $expect_match) : bool {

    $result = TRUE;
    
//print strlen($c1).",".strlen($c2)."\n";

    if ((strlen($c1) == 0) || (strlen($c2) == 0)) {
      $result = FALSE;
      $msg = "zero length content";
    } else if (($c1 == $c2) XOR $expect_match) {
      $result = FALSE;
      if ($expect_match) {
        $msg = "files mismatch";
      } else {
        $msg = "files match";
      }
    } else {
      if (! $expect_match) {
        $msg = "files mismatch";
      } else {
        $msg = "files match";
      }
    }
    
    print_report_update_successes  ($tests_count,
                                    $successes_count,
                                    $result,
                                    $msg,
                                    $msg,
                                    $test_name,
                                    $test_group_name,
                                    FALSE);
    
    return $result;
  }
  */
/*
  function prepare_and_write_debugger_exec_file (string $debugger_exec) {
    if (!isset($debugger_exec) || ($debugger_exec == "")) {
      // default shutdown paradigm (for cat-on-startup cases)
      // set breakpoint at &FEFF in I/O space, then CALL it to exit
      $debugger_exec = "b feff\nbx 0\npaste CALL &FEFF|M\nc\nc\nc\n\n\n";
    }

    // set up debugger script
    @unlink(PATH_DEBUGGER_EXEC_TMPFILE);
    if (FALSE === file_put_contents (PATH_DEBUGGER_EXEC_TMPFILE, $debugger_exec)) {
      print "FATAL: Cannot write tmpfile: ".PATH_DEBUGGER_EXEC_TMPFILE."\n";
      die();
    }
  }*/

  function prepare_command_line (string $tape_to_load,
                                 int $tapetest_bits,
                                 string $extra_args,
                                 bool $verbose,
                                 string $path_sep,
                                 bool $slow_startup,
                                 bool $show_tapectrl,
                                 string $cfgfile) {

//    if ((strlen($extra_args) > 0) && ($extra_args[strlen($extra_args)-1] != " ")) { $extra_args .= " "; }

    if ($tape_to_load != "") {
      $extra_args = " -tape ".$tape_to_load." ".$extra_args;
    }

    if ($show_tapectrl) {
      $tapetest_bits |= 32; /* TOHv4.3: always spawn tapectrl */
    }

    if ($slow_startup) { $tapetest_bits |= 16; } // TOHv4.1: slow startup option

    // TOHv4-rc4: now send '-tapetest 0' (or 16 if slow startup)
    // in order to force memory display window suppression even
    // if no tapetest quit condition was specified
    $extra_args = " -tapetest $tapetest_bits $extra_args";

    $cl = B_EM_EXE.
          " -sp4 ".
          "-exec ".PATH_DEBUGGER_EXEC_TMPFILE." ".
          "-cfg $cfgfile ". // TTv6, for TOHv4.3
          " -debug $extra_args"; // -bp-shutdown

    if ($verbose) {
        print "$cl\n";
    }

    return $cl;

  }

  function run_write_debugger_exec_file (string $debugger_exec) {

    if ($debugger_exec == "") {
      // default shutdown paradigm (for cat-on-startup cases)
      // set breakpoint at &4000 in I/O space, then CALL it to exit
      $debugger_exec = "b 4000\nbx 0\npaste ?&4000=&60|MCALL &4000|M\nc\n";
    }

    // set up debugger script
    @unlink(PATH_DEBUGGER_EXEC_TMPFILE);
    if (FALSE === file_put_contents (PATH_DEBUGGER_EXEC_TMPFILE, $debugger_exec)) {
      print "FATAL: Cannot write tmpfile: ".PATH_DEBUGGER_EXEC_TMPFILE."\n";
      die();
    }

  }

  // returns contents of $opfile_to_read
  function run_without_testing (TestsState $ts,
                                string $debugger_exec,      // string to send to debugger (via PATH_DEBUGGER_EXEC_TMPFILE)
                                string $tape_to_load,       // tape to load (automatic -tape on command-line, with correct path)
                                string $extra_args,         // b-em command-line (extras)
                                int $tapetest_bits,         // tape test mode, usually 7 (=quit on err + quit on EOF + cat on startup); use 0 here for none
                                $opfile_to_read,            // may be NULL
                                bool $skip,
                                int $expiry_secs,
                                string $cfgfile) : string {
    global $sep;
    if ($skip) { return ""; }
    $op="";
    $e = -1;
    run_write_debugger_exec_file($debugger_exec);
    if (isset($opfile_to_read)) { @unlink($opfile_to_read); }
//    if ((strlen($extra_args)>0) && ($extra_args[strlen($extra_args) - 1] != " ")) { $extra_args .= " "; } // append space to extra_args if needed
    $extra_args .= " -expire $expiry_secs ";
    $cl = prepare_command_line($tape_to_load,
                               $tapetest_bits,
                               $extra_args,
                               $ts->verbose,
                               $sep,
                               $ts->slow_startup,
                               $ts->show_tapectrl,
                               $cfgfile);
    if (FALSE === exec ($cl, $op, $e)) {
      return "";
    }
    $opfile_contents = "";
    if (isset($opfile_to_read)) {
      $opfile_contents = @file_get_contents($opfile_to_read);
      if (FALSE === $opfile_contents) {$opfile_contents="";}
    }
    return $opfile_contents;
  }
  
  

  function run (TestsState &$ts,
                string $debugger_exec,    // string to send to debugger (via PATH_DEBUGGER_EXEC_TMPFILE)
                string $tape_to_load,     // tape to load (automatic -tape on command-line, with correct path)
                string $extra_args,       // b-em command-line (extras)
                string $test_group_name,  // which test group this test is in
                int $expected_error_code,
                int $tapetest_bits,       // tape test mode, usually 7 (=quit on err + quit on EOF + cat on startup); use 0 here for none
                string $test_name,
                bool $skip,
                array $all_required_stdout_lines, // only lines that start with "tapetest:" prefix, so [0] will be the first "tapetest" line etc.
                int $expiry_secs,
                string $cfgfile) : void {
                
    global $sep;
    global $errnames;

    $result = FALSE;
    $e = 0;
    $msg = "";

    if ( $skip ) {
      $ts->skips++;
//      return;
    } else {
    
      // TOHv4.1: sanity check; make sure -tape file actually exists
      if (($tape_to_load != "") && (! @file_exists($tape_to_load))) {
          print "FATAL: Tape file does not exist: $tape_to_load for test: $test_name\n";
          die();
      }

      run_write_debugger_exec_file($debugger_exec);

      if (isset($opfile)) {
        @unlink($opfile);
      }
        
      $extra_args .= " -expire $expiry_secs ";
      // $extra_args .= " -tapectrl "; /* TTv6 -- always invoke tapectrl */
      $cl = prepare_command_line($tape_to_load,
                                 $tapetest_bits,
                                 $extra_args,
                                 $ts->verbose,
                                 $sep,
                                 $ts->slow_startup,
                                 $ts->show_tapectrl,
                                 $cfgfile);
      $msg=""; // for print_report_update_successes()
      $op=array();  // collect stdout from b-em
      
      $e = -1;
      $result = TRUE; // result of exec() call
      
      if (FALSE === exec ($cl, $op, $e)) {
        $result = FALSE;
        $msg = "exec";
      }

// print "ret $e\n";
      
      if ($ts->spew) { print_r($op); print "\n"; } // verbose
        
  //print_r($all_required_stdout_matches);

      $total = 0;
      
      if ($result && (count($all_required_stdout_lines) > 0)) {
      
        // $i=0;
        //foreach ($op as $_=>$op_line) {
        if (0 == count($op)) {
          $result = FALSE;
  //          $msg = "nil. L".($i+1);
          $msg="blank op";
        } else {
          $total = 0; // 3.3: flag is now a counter
          for ($i=0, $j=0;
               ($i < count($op));
               $i++) {
            $op_line = $op[$i];
            $x = MAGIC_TAPETEST_STDOUT_LINE;
            $len = strlen($x);
            $sub1 = substr($op_line, 0, $len); // strip "tapetest:" pfx
  //print "sub=$sub1\n";
            if ($sub1 != MAGIC_TAPETEST_STDOUT_LINE) {
              continue; // not a "tapetest:" line, skip
            }
            $sub2 = substr($op_line, $len, strlen($op_line) - $len);

  /*
  $prev_total = $total;
            foreach ($all_required_stdout_matches as $_=>$rx) {
              // how to match? originally required explicit match,
              // we'll downgrade this a bit to just needing a non-FALSE strpos() result
              //if ($rx == $sub2) {
              if (FALSE !== strpos($sub2, $rx)) {
                $total++; // 3.3: counter
  print "\"$sub2\" == \"$rx\"\n";
                //break;
              }
            }
  print $total."\n";
  if ( $total == $prev_total ) { print $rx."\n"; die(); }
  */


  // print "all_required_stdout_matches[$total] = $all_required_stdout_matches[$total]\n";
  // print "sub2 = $sub2\n";

  //            if ($all_required_stdout_matches[$total] != $sub2) {

  //            if (isset($all_required_stdout_lines[$total]) && (FALSE === strstr($sub2, $all_required_stdout_lines[$total]))) {
            if (isset($all_required_stdout_lines[$total]) && ($sub2 != $all_required_stdout_lines[$total])) {
              $msg = "op bad";
              $result = FALSE;
              break;
            }

            $total++;

          } // next op line
          // 3.3
  //          if ($total != count($all_required_stdout_matches)) {
  //            $result = FALSE;
  //            $msg = "op bad";
  //          }
        } // endif (have output)
      } // endif (have some lines to check against output)

      if ($total != count($all_required_stdout_lines)) {
        $msg = "mismatch";
        $result = FALSE;
      }

  //
  //if (count($all_required_stdout_matches)) {
  //print "$total\n";
  //print count($all_required_stdout_matches)."\n";
  //
  //die(); }
  //
      if ($result) { // exec() succeeded?
        if ($expected_error_code != $e) { // but exit code is wrong
          $result = FALSE;
          if ( ! isset($errnames[$e]) ) {
              $en = "? (code $e)";
          } else {
              $en = $errnames[$e];
          }
          if ( ! isset ($errnames[$expected_error_code]) ) {
              $eec = "? (code $expected_error_code)";
          } else {
              $eec = $errnames[$expected_error_code];
          }
          $msg = "$en!=$eec";
        }
      }
        
      // check tape output file
      if ($result && isset($opfile) && (file_exists($opfile) XOR $expect_opfile)) {
        $result = FALSE;
        $msg = ($expect_opfile ? "no output" : "output");
      }
      
    }  //endif ($skip)
    
    print_report_update_successes  ($ts,
                                    $result,
                                    (isset($errnames[$e])) ? $errnames[$e] : "? code $e", //strval($e),
                                    $msg,
                                    $test_name,
                                    $test_group_name,
                                    $skip);
    
  }
  
  
  /* returns list of chunks */
  function parse_uef (string $s, array &$chunks_out) : int {
  
    if (0==strlen($s)) {
        return E_PARSE_UEF_NO_INPUT;
    }
  
    $chunks_out = array();

    if (($s[0] == "\x1f") && ($s[1] == "\x8b")) {
      //print "compressed; gunzipping ...\n";
      $s = gzdecode($s);
      if (FALSE === $s) {
//print "E: gzdecode() failed.\n";
        return E_UEF_GZDECODE;
      }
    }

    $len = strlen($s);

    if (substr($s, 0, 10) != FILEMAGIC_UEF."\0") { //) {
      return E_UEF_MAGIC;
    }
    
    if (substr($s, 10, 2) != "\x0a\x00") {
      return E_UEF_VERSION;
    }

    $p = 12; // skip file header

    //$cn=0;

    // transpo_hexdump($s);

    while ($p < $len) {
      //printf("off 0x%x, ", $p);
      $type = ord($s[$p]) | (ord ($s[$p+1]) << 8);
// printf("TYPE &%x\n", $type);
      $p += 2;
      $chklen = ord($s[$p]) | (ord ($s[$p+1]) << 8) | (ord ($s[$p+2]) << 16) | (ord ($s[$p+3]) << 24);
// print "len $chklen\n";
      $p += 4;
      $data = substr($s, $p, $chklen);
// transpo_hexdump($data);
// for ($z=0;$z<$chklen;$z++) { printf("%x ", $data[$z]); }
// print"\n";
      // sanity
      if ($type & 0xf000) {
        //print "ERR: bad chunk type &".sprintf("%x", $type)."\n";
        return E_UEF_INSANE_TYPE;
      }
//printf ("type &%x\n", $type);
      $a = array('data'=>$data, 'type'=>$type);
      $chunks_out[] = $a;
      $p += $chklen;
      //$cn++;
    }
//print "--\n";

// transpo_hexdump($chunks_out[3]["data"]);
    
    return E_OK;
    
  }
  
  
  function get_origin_000_count (string $uef_data, int &$count_out) : int {
    $chunks = array();
    $count_out = 0;
    $e = parse_uef ($uef_data, $chunks);
    if (E_OK != $e) { return $e; }
    foreach ($chunks as $lol=>$chunk) {
      $count_out += ((0 == $chunk['type']) ? 1 : 0);
    }
    return E_OK;
  }
  
  
  function expect_uef_117_sequence (string $uef_data, array $baudlist) : int {
//print_r($baudlist);
    $chunks = array();
    $e = parse_uef ($uef_data, $chunks);
//print "parse_uef returns $e\n";
    if (E_OK != $e) { return $e; }
//print_r($chunks);
    $count=0;
    $e = E_OK;
    foreach ($chunks as $ix=>$tmp) {
      $type = $tmp['type'];
      $data = $tmp['data'];
//printf("  [$ix,$count/".count($chunks)."] type &%x len %d\n", $type, strlen($data));
      if ($type==0x117) {
        if (strlen($data) != 2) {
          return E_UEF_117_LEN;
        }
        $baud = 0;
        $d = substr($data, 0, 2);
        if ($d == "\xb0\x04") {
          $baud = 1200;
        } else if ($d == "\x2c\x01") {
          $baud = 300;
        } else {
          return E_UEF_117_RATE;
        }
//print "117: baud is $baud\n";
        if ( ! isset ($baudlist[$count] ) ) {
            // extra 117s not included in baud list
            return E_UEF_117_EXCESS;
        }
        if ($baud != $baudlist[$count]) {
          return E_UEF_117_BAUD;
        }
        $count++;
      }
    }
    if ($count != count($baudlist)) {
      return E_UEF_117_COUNT;
    }
    return $e;
  }
  
  
  /*
  
  // !!!!! TODO !!!!!
  // These will just use b-em again, to check generated output files.
  function check_csw (string $fn) : bool {
    return TRUE;
  }
  
  function check_tibet (string $fn) : bool {
    //return TRUE;
    
    $args = $fn;
    $op="";
    $e=0;
    if (FALSE === exec (TIBET_DECODER." ".$args, $op, $e)) {
      $result = FALSE;
      $msg = "exec";
    }
//print_r($op);
    return ($e == 0);
    
  }
  
  function check_uef (string $fn) : bool {
    return TRUE;
  }
  
  function check_tibetz (string $fn) : bool {
    @unlink(TIBET_TMPFILE);
    if (FALSE === ($gz_data = file_get_contents($fn))) { return FALSE; }
    $gunz = gzdecode($gz_data);
    if (FALSE === $gunz) { return FALSE; }
    if (FALSE === file_put_contents(TIBET_TMPFILE, $gunz)) { return FALSE; }
    $e = check_tibet(TIBET_TMPFILE);
    @unlink(TIBET_TMPFILE);
    return $e;
  }
  
  function test_tibet_concat (string $tibet_file_contents) : bool {
    $tibet_file_contents .= $tibet_file_contents;
    @unlink(TIBET_TMPFILE);
    if (FALSE === file_put_contents(TIBET_TMPFILE, $tibet_file_contents)) { return FALSE; }
    $e = check_tibet(TIBET_TMPFILE);
    @unlink(TIBET_TMPFILE);
    return $e;
  }
  
  */
  
  
  function tibet_tonechar_run_len_check (string $tibet_s, int $num_1200ths) : int {
    $tibet_s = str_replace("\n", "", $tibet_s);
    $m = array();
    // extract and purify all data spans
    preg_match_all("/data([.-]*)end/",$tibet_s,$m);
    if (!isset($m) || (FALSE===$m) || (2 != count($m))) {
      return E_BAUD_RUN_LEN;
    }
    $data_spans = $m[1];
    // now have all data spans in $m ...
    $shortest=1000;
    foreach($data_spans as $_=>$tones) {
//      print $tones."\n\n";
      for ($i=0, $run_len=0, $run_char = $tones[0];
           $i < strlen($tones);
           $i++, $run_len++) {
        if ($tones[$i] != $run_char) {
          // EOR
//print "$run_len\n";
          // want exact multiple
          $f = (((float) $run_len) / ($num_1200ths * 2.0));
          if ($f < 1.0) { return E_BAUD_RUN_LEN; }
          if (fmod($f, 1) > 0.00001) { return E_BAUD_RUN_LEN; }
          if ($run_len < $shortest) {
              $shortest = $run_len;
          }
//          if ($run_len != ($num_1200ths * 2)) { return E_BAUD_RUN_LEN; }
//print $run_len."\n";
          $run_len=0;
          $run_char = $tones[$i];
        }
      }
    }
    if ($shortest != ($num_1200ths * 2)) {
      return E_BAUD_RUN_LEN;
    }
//    print_r($m);
//    die();
//print "lol";die();
    return E_OK;
  }

  function get_aborted_block_tibet_output() : string {
    return  "tibet 0.5\n\n".
            "/baud 1200\n".
            "/framing 8N1\n".
            "data\n".
            "----..--..--\n".
            "end\n\n".
            "leader 2\n\n".
            "/baud 1200\n".
            "/framing 8N1\n".
            "data\n".
            "----..--..--..--....\n".
            "end\n\n".
            "leader 12198\n\n".
            "/baud 1200\n".
            "/framing 8N1\n".
            "data\n".
            "----..--..--..----..\n". // sync
            "----------....----..\n". // fn: '0'
            "--..------....----..\n". // fn: '1'
            "----..----....----..\n". // fn: '2'
            "--....----....----..\n". // fn: '3'
            "------..--....----..\n". // fn: '4'
            "--..--..--....----..\n". // fn: '5'
            "----....--....----..\n". // fn: '6'
            "--......--....----..\n". // fn: '7'
            "--------......----..\n". // fn: '8' : final filename characters '9' and '\0' are lost in ACIA TX pipeline
            "end\n\n";
  }
  
  function get_evil_114_contents_array() : array {
    return array(
      "0:00:00 MENU       0f8f ffff1900 ffff8023",
      "0:00:41 USIDEMO    091f ffff1f00 ffff8023",
      "0:01:06 M.USI      04a4 00001300 00001300",
      "0:01:18 USI        2057 ffff1f00 ffff8023",
      "0:02:43 PRESSC     04d1 ffff1f00 ffff8023",
      "0:02:55 RAMSAFE    19d3 ffff1f00 ffff8023",
      "0:04:04 TIMER      0d78 ffff1900 ffff8023",
      "0:04:39 COMPILE    33a6 ffff1900 ffff8023",
      "0:06:55 EVAL2A     02ea ffff0e00 ffff802b",
      "0:07:03 EVAL3      02cf ffff1900 ffff8023",
      "0:07:11 EVAL4      0388 ffff1900 ffff8023",
      "0:07:21 BURGER     1d2b ffff1900 ffff8023",
      "0:08:38 BIBLIO     064f 00000800 00008023",
      "0:08:55 MSCN409    0717 ffffffff ffffffff",
      "0:09:14 BJACK      0500 00007000 0000740f",
      "0:09:28 BJACK2     3200 00002000 00003d00",
      "0:11:39 MENU       0f8f ffff1900 ffff8023",
      "0:12:20 USIDEMO    091f ffff1f00 ffff8023",
      "0:12:44 M.USI      04a4 00001300 00001300",
      "0:12:57 USI        2057 ffff1f00 ffff8023",
      "0:14:21 PRESSC     04d1 ffff1f00 ffff8023",
      "0:14:34 RAMSAFE    19d3 ffff1f00 ffff8023",
      "0:15:42 TIMER      0d78 ffff1900 ffff8023",
      "0:16:18 COMPILE    33a6 ffff1900 ffff8023",
      "0:18:34 EVAL2A     02ea ffff0e00 ffff802b",
      "0:18:42 EVAL3      02cf ffff1900 ffff8023",
      "0:18:50 EVAL4      0388 ffff1900 ffff8023",
      "0:19:00 BURGER     1d2b ffff1900 ffff8023",
      "0:20:17 BIBLIO     064f 00000800 00008023",
      "0:20:34 MSCN409    0717 ffffffff ffffffff",
      "0:20:53 BJACK      0500 00007000 0000740f",
      "0:21:06 BJACK2     3200 00002000 00003d00",
      "0:23:18 MENU       0f6f ffff1900 ffff802b",
      "0:23:58 STATUS     0878 ffff1f00 ffff1f00",
      "0:24:21 SCOPE      13a7 ffff1f00 ffff802b",
      "0:25:13 COMPARE    1337 ffff1f00 ffff1f00",
      "0:26:04 FLEXDMP    173c ffff1f00 ffff1f00",
      "0:27:05 INKEY      022d ffff1f00 ffff802b",
      "0:27:12 ADFSMEN    14fc ffff1f00 ffff802b",
      "0:28:07 BBDATE     15a1 ffff1f00 ffff1f00",
      "0:29:04 BBDATE1    051b ffff1f00 ffff802b",
      "0:29:18 BBDATE2    0440 ffff1f00 ffff802b",
      "0:29:30 FUNMEN     0209 ffff1f00 ffff802b",
      "0:29:36 FILERA     1d3f ffff1f00 ffff802b",
      "0:30:53 BANK       1b90 00000000 00000000",
      "0:32:06 P.BANK     036c 00000000 00000000",
      "0:32:15 TUTAN      1d68 ffff1f00 ffff802b",
      "0:33:32 BIBLIO     078d ffff1f00 ffff802b",
      "0:33:53 MSCN505    07b2 00003000 00003000",
      "0:34:13 FORUM      02f4 ffff1f00 ffff1f00",
      "0:34:21 MENU       0f6f ffff1900 ffff802b",
      "0:35:02 STATUS     0878 ffff1f00 ffff1f00",
      "0:35:25 SCOPE      13a7 ffff1f00 ffff802b",
      "0:36:16 COMPARE    1337 ffff1f00 ffff1f00",
      "0:37:08 FLEXDMP    173c ffff1f00 ffff1f00",
      "0:38:09 INKEY      022d ffff1f00 ffff802b",
      "0:38:16 ADFSMEN    14fc ffff1f00 ffff802b",
      "0:39:11 BBDATE     15a1 ffff1f00 ffff1f00",
      "0:40:08 BBDATE1    051b ffff1f00 ffff802b",
      "0:40:22 BBDATE2    0440 ffff1f00 ffff802b",
      "0:40:34 FUNMEN     0209 ffff1f00 ffff802b",
      "0:40:40 FILERA     1d3f ffff1f00 ffff802b",
      "0:41:57 BANK       1b90 00000000 00000000",
      "0:43:09 P.BANK     036c 00000000 00000000",
      "0:43:19 TUTAN      1d68 ffff1f00 ffff802b",
      "0:44:36 BIBLIO     078d ffff1f00 ffff802b",
      "0:44:56 MSCN505    07b2 00003000 00003000",
      "0:45:17 FORUM      02f4 ffff1f00 ffff1f00",
      "0:45:25 MENU       1273 ffff0e00 ffff802b",
      "0:46:14 CARPARK    12ff ffff0e00 ffff802b",
      "0:47:04 MULTCOL    0ef1 ffff0e00 ffff802b",
      "0:47:43 CREATE     02b4 ffff0e00 ffff802b",
      "0:47:51 DISPLAY    0227 ffff0e00 ffff802b",
      "0:47:57 UPDATE     02c8 ffff0e00 ffff802b",
      "0:48:05 Dates      0046 ffffffff ffffffff",
      "0:48:06 NOTE       0237 ffff0e00 ffff802b",
      "0:48:12 EDITOR     19a2 00000000 ffffffff",
      "0:49:20 FAPPEND    020a 00000000 ffffffff",
      "0:49:26 MWSOURC    3565 ffff0e00 ffff802b",
      "0:51:47 MWrom      0afb 00008000 00008000",
      "0:52:16 DSPOOL     0b25 ffff0e00 ffff802b",
      "0:52:46 DISCSPL    00f8 00000900 00000900",
      "0:52:48 STRSORT    0275 ffff0e00 ffff802b",
      "0:52:55 PASSWOR    03b6 ffff0e00 ffff802b",
      "0:53:06 FUNCTIO    0190 ffff0e00 ffff802b",
      "0:53:10 VECTOR1    0345 ffff0e00 ffff802b",
      "0:53:19 VECTOR2    0e33 ffff0e00 ffff802b",
      "0:53:57 COMMAS     0434 ffff0e00 ffff802b",
      "0:54:09 MF-EXAM    079c ffff0e00 ffff802b",
      "0:54:29 ASSEM1     0311 ffff0e00 ffff802b",
      "0:54:38 ASSEM2     09e6 ffff0e00 ffff802b",
      "0:55:04 BBC-IBM    1a76 ffff0e00 ffff802b",
      "0:56:14 BIBLIO     08ee ffff0e00 ffff802b",
      "0:56:38 MSCN701    07a0 00004556 00024353",
      "0:56:59 VOL6       57f1 ffffffff ffffffff",
      "1:00:48 MENU       107f ffff0e00 ffff802b",
      "1:01:31 XW         1452 ffff1400 ffff802b",
      "1:02:25 PROG1      059a ffff0e00 ffff802b",
      "1:02:40 PROG2      0960 ffff0e00 ffff802b",
      "1:03:05 WORM       14ec ffff0e00 ffff802b",
      "1:04:00 MWPROG     0310 ffff0e00 ffff802b",
      "1:04:09 MWROM      0b00 ffffffff ffffffff",
      "1:04:38 MINI       0925 ffff0e00 ffff802b",
      "1:05:03 VIEWF      0591 ffff0e00 ffff802b",
      "1:05:18 French     055b ffff0e00 ffff802b",
      "1:05:33 German     069f ffff0e00 ffff802b",
      "1:05:51 Spanish    07ba ffff0e00 ffff802b",
      "1:06:12 Turkish    0667 ffff0e00 ffff802b",
      "1:06:30 Greek      0ca0 ffff0e00 ffff802b",
      "1:07:03 MATT3      2d4c ffff0e00 ffff802b",
      "1:09:02 MAT        06ea 00007500 00007500",
      "1:09:21 S          0acf ffff0e00 ffff802b",
      "1:09:49 FILES41    138f ffff0e00 ffff802b",
      "1:10:41 UASS2-1    04d7 ffff0e00 ffff802b",
      "1:10:55 UASS2-2    074a ffff0e00 ffff802b",
      "1:11:15 UASS2-3    0627 ffff0e00 ffff802b",
      "1:11:32 WORDSOR    040f ffff0e00 ffff802b",
      "1:11:43 COMPARE    0c78 ffff0e00 ffff802b",
      "1:12:17 BIBLIO     08ee ffff0e00 ffff802b",
      "1:12:40 MSCN704    05e9 00004556 00024353",
      "1:12:56 MENU       107f ffff0e00 ffff802b",
      "1:13:40 XW         1452 ffff1400 ffff802b",
      "1:14:33 PROG1      059a ffff0e00 ffff802b",
      "1:14:48 PROG2      0960 ffff0e00 ffff802b",
      "1:15:13 WORM       14ec ffff0e00 ffff802b",
      "1:16:08 MWPROG     0310 ffff0e00 ffff802b",
      "1:16:16 MWROM      0b00 ffffffff ffffffff",
      "1:16:45 MINI       0925 ffff0e00 ffff802b",
      "1:17:10 VIEWF      0591 ffff0e00 ffff802b",
      "1:17:25 French     055b ffff0e00 ffff802b",
      "1:17:39 German     069f ffff0e00 ffff802b",
      "1:17:57 Spanish    07ba ffff0e00 ffff802b",
      "1:18:18 Turkish    0667 ffff0e00 ffff802b",
      "1:18:35 Greek      0ca0 ffff0e00 ffff802b",
      "1:19:09 MATT3      2d4c ffff0e00 ffff802b",
      "1:21:08 MAT        06ea 00007500 00007500",
      "1:21:26 S          0acf ffff0e00 ffff802b",
      "1:21:54 FILES41    138f ffff0e00 ffff802b",
      "1:22:46 UASS2-1    04d7 ffff0e00 ffff802b",
      "1:22:59 UASS2-2    074a ffff0e00 ffff802b",
      "1:23:19 UASS2-3    0627 ffff0e00 ffff802b",
      "1:23:36 WORDSOR    040f ffff0e00 ffff802b",
      "1:23:47 COMPARE    0c78 ffff0e00 ffff802b",
      "1:24:20 BIBLIO     08ee ffff0e00 ffff802b",
      "1:24:44 MSCN704    05e9 00004556 00024353",
      "1:25:00 MENU       1196 ffff0e00 ffff802b",
      "1:25:46 ASTAAD3    1a76 ffff0e00 ffff802b",
      "1:26:56 ASTAAD     1457 ffff0e00 ffff802b",
      "1:27:50 CHARTES    0b67 ffff0e00 ffff802b",
      "1:28:20 MOUSEPR    0bd6 ffff0e00 ffff802b",
      "1:28:52 TEST       0746 ffff0e00 ffff802b",
      "1:29:11 S-A        03a3 ffff0e00 ffff802b",
      "1:29:21 ST1        0401 ffff0e00 ffff802b",
      "1:29:32 ST2        021f ffff0e00 ffff802b",
      "1:29:38 ST3        02a3 ffff0e00 ffff802b",
      "1:29:46 SORTS      09f7 ffff0e00 ffff802b",
      "1:30:12 SQUEEZE    33b6 ffff0e00 ffff802b",
      "1:32:29 SQZOBJ     0b5e 00008000 00008000",
      "1:32:59 UASS3-1    0401 ffff0e00 ffff802b",
      "1:33:11 UASS3-2    0996 ffff0e00 ffff802b",
      "1:33:37 FILES51    1b4f ffff0e00 ffff802b",
      "1:34:49 EXAM       02cc 00000000 00000000",
      "1:34:57 VLIST      0290 ffff0e00 ffff802b",
      "1:35:05 HEAP       0705 ffff0e00 ffff802b",
      "1:35:24 ELEVEN     18ab ffff0e00 ffff802b",
      "1:36:29 BIBLIO     089f ffff0e00 ffff802b",
      "1:36:52 MSCN705    05a4 00014556 00020d73",
      "1:37:08 MENU       1196 ffff0e00 ffff802b",
      "1:37:54 ASTAAD3    1a76 ffff0e00 ffff802b",
      "1:39:04 ASTAAD     1457 ffff0e00 ffff802b",
      "1:39:58 CHARTES    0b67 ffff0e00 ffff802b",
      "1:40:28 MOUSEPR    0bd6 ffff0e00 ffff802b",
      "1:41:00 TEST       0746 ffff0e00 ffff802b",
      "1:41:19 S-A        03a3 ffff0e00 ffff802b",
      "1:41:29 ST1        0401 ffff0e00 ffff802b",
      "1:41:40 ST2        021f ffff0e00 ffff802b",
      "1:41:47 ST3        02a3 ffff0e00 ffff802b",
      "1:41:54 SORTS      09f7 ffff0e00 ffff802b",
      "1:42:20 SQUEEZE    33b6 ffff0e00 ffff802b",
      "1:44:37 SQZOBJ     0b5e 00008000 00008000",
      "1:45:07 UASS3-1    0401 ffff0e00 ffff802b",
      "1:45:19 UASS3-2    0996 ffff0e00 ffff802b",
      "1:45:45 FILES51    1b4f ffff0e00 ffff802b",
      "1:46:57 EXAM       02cc 00000000 00000000",
      "1:47:05 VLIST      0290 ffff0e00 ffff802b",
      "1:47:12 HEAP       0705 ffff0e00 ffff802b",
      "1:47:32 ELEVEN     18ab ffff0e00 ffff802b",
      "1:48:37 BIBLIO     089f ffff0e00 ffff802b",
      "1:49:00 MSCN705    05a4 00014556 00020d73",
      "1:49:16 DMENU      1d95 ffff0e00 ffff802b",
      "1:50:34 Centre     0f3b ffff1900 ffff8023",
      "1:51:14 person     0163 ffffffff ffffffff",
      "1:51:18 ZAP        1284 ffff0e00 ffff802b",
      "1:52:07 LOADER     06e6 ffff0e00 ffff802b",
      "1:52:25 EDIT       122b ffff0e00 ffff802b",
      "1:53:13 wkfile     0037 00000000 ffffffff",
      "1:53:15 HELPSHE    03fb ffffffff ffffffff",
      "1:53:25 MENU       0291 ffff0e00 ffff802b",
      "1:53:33 PRINT      0887 ffff0e00 ffff802b",
      "1:53:56 MANAGER    0279 00008000 00008000",
      "1:54:03 SMANAGE    0dac ffff0e00 ffff802b",
      "1:54:39 SW-TEST    0130 ffffffff ffffffff",
      "1:54:43 CLOCK      2808 ffff0e00 ffff802b",
      "1:56:29 STEAMS     192a ffff0e00 ffff802b",
      "1:57:36 GRAPH7     051c ffff0e00 ffff802b",
      "1:57:50 FILES      003a ffff0e00 ffff802b",
      "1:57:51 FileTxt    02a0 00000000 ffffffff",
      "1:57:59 FILES86    274a ffff0e00 ffff802b",
      "1:59:43 ORDERS     0420 00000000 00000000",
      "1:59:55 ITEMS      02f4 00000000 00000000",
      "2:00:03 DRAUGHT    1e9d ffff0e00 ffff802b",
      "2:01:24 BIBLIO     089f ffff0e00 ffff802b",
      "2:01:47 MSCN708    0568 00000000 ffffffff",
      "2:02:02 MENU       23bc ffff0e00 ffff802b",
      "2:03:36 3DLands    0dc3 ffff0e00 ffff802b",
      "2:04:12 3DLandB    0d39 ffff0e00 ffff802b",
      "2:04:48 PAGER1     0601 ffff0e00 ffff802b",
      "2:05:04 Pager2     1884 ffff0e00 ffff802b",
      "2:06:09 DualCol    1700 ffff0e00 ffff802b",
      "2:07:09 Centre     0f3b ffff0e00 ffff8023",
      "2:07:50 DISPLAY    11fa ffff0e00 ffff802b",
      "2:08:37 Person1    0163 ffffffff ffffffff",
      "2:08:42 Person2    0163 ffffffff ffffffff",
      "2:08:46 AWIPE      0b10 ffff0e00 ffff802b",
      "2:09:15 FP         09fe ffff0e00 ffff802b",
      "2:09:41 FPcode     0428 00006000 00006000",
      "2:09:53 FPsrc      1442 ffff0e00 ffff802b",
      "2:10:46 Dater      06b5 ffff0e00 ffff802b",
      "2:11:04 DATCON     09d3 ffff0e00 ffff802b",
      "2:11:30 Time1      012b ffff0e00 ffff802b",
      "2:11:34 Rally1     1504 ffff0e00 ffff802b",
      "2:12:30 Coaster    0de9 ffff0e00 ffff802b",
      "2:13:07 DATA       1900 00000e00 00000e00",
      "2:14:12 BIBLIO     089f ffff0e00 ffff802b",
      "2:14:35 MSCN709    0b2e 00000000 ffffffff",
      "2:15:05 WordWra    00c0 ffff0e00 ffff802b"
    );
  }

  // for checking 5 hour UEF's catalogue with -tapetest 7 ...
  function get_5hour_contents_array() {
    return array(
    /* old version without timestamps: */
    /*
      "MENU       0f6f ffff1900 ffff802b",
      "STATUS     0878 ffff1f00 ffff1f00",
      "SCOPE      13a7 ffff1f00 ffff802b",
      "COMPARE    1337 ffff1f00 ffff1f00",
      "FLEXDMP    173c ffff1f00 ffff1f00",
      "INKEY      022d ffff1f00 ffff802b",
      "ADFSMEN    14fc ffff1f00 ffff802b",
      "BBDATE     15a1 ffff1f00 ffff1f00",
      "BBDATE1    051b ffff1f00 ffff802b",
      "BBDATE2    0440 ffff1f00 ffff802b",
      "FUNMEN     0209 ffff1f00 ffff802b",
      "FILERA     1d3f ffff1f00 ffff802b",
      "BANK       1b90 00000000 00000000",
      "P.BANK     036c 00000000 00000000",
      "TUTAN      1d68 ffff1f00 ffff802b",
      "BIBLIO     078d ffff1f00 ffff802b",
      "MSCN505    07b2 00003000 00003000",
      "FORUM      02f4 ffff1f00 ffff1f00",
      "MENU       0f8f ffff1900 ffff8023",
      "USIDEMO    091f ffff1f00 ffff8023",
      "M.USI      04a4 00001300 00001300",
      "USI        2057 ffff1f00 ffff8023",
      "PRESSC     04d1 ffff1f00 ffff8023",
      "RAMSAFE    19d3 ffff1f00 ffff8023",
      "TIMER      0d78 ffff1900 ffff8023",
      "COMPILE    33a6 ffff1900 ffff8023",
      "EVAL2A     02ea ffff0e00 ffff802b",
      "EVAL3      02cf ffff1900 ffff8023",
      "EVAL4      0388 ffff1900 ffff8023",
      "BURGER     1d2b ffff1900 ffff8023",
      "BIBLIO     064f 00000800 00008023",
      "MSCN409    0717 ffffffff ffffffff",
      "BJACK      0500 00007000 0000740f",
      "BJACK2     3200 00002000 00003d00",
      "MENU       1273 ffff0e00 ffff802b",
      "CARPARK    12ff ffff0e00 ffff802b",
      "MULTCOL    0ef1 ffff0e00 ffff802b",
      "CREATE     02b4 ffff0e00 ffff802b",
      "DISPLAY    0227 ffff0e00 ffff802b",
      "UPDATE     02c8 ffff0e00 ffff802b",
      "Dates      0046 ffffffff ffffffff",
      "NOTE       0237 ffff0e00 ffff802b",
      "EDITOR     19a2 00000000 ffffffff",
      "FAPPEND    020a 00000000 ffffffff",
      "MWSOURC    3565 ffff0e00 ffff802b",
      "MWrom      0afb 00008000 00008000",
      "DSPOOL     0b25 ffff0e00 ffff802b",
      "DISCSPL    00f8 00000900 00000900",
      "STRSORT    0275 ffff0e00 ffff802b",
      "PASSWOR    03b6 ffff0e00 ffff802b",
      "FUNCTIO    0190 ffff0e00 ffff802b",
      "VECTOR1    0345 ffff0e00 ffff802b",
      "VECTOR2    0e33 ffff0e00 ffff802b",
      "COMMAS     0434 ffff0e00 ffff802b",
      "MF-EXAM    079c ffff0e00 ffff802b",
      "ASSEM1     0311 ffff0e00 ffff802b",
      "ASSEM2     09e6 ffff0e00 ffff802b",
      "BBC-IBM    1a76 ffff0e00 ffff802b",
      "BIBLIO     08ee ffff0e00 ffff802b",
      "MSCN701    07a0 00004556 00024353",
      "VOL6       57f1 ffffffff ffffffff",
      "MENU       107f ffff0e00 ffff802b",
      "XW         1452 ffff1400 ffff802b",
      "PROG1      059a ffff0e00 ffff802b",
      "PROG2      0960 ffff0e00 ffff802b",
      "WORM       14ec ffff0e00 ffff802b",
      "MWPROG     0310 ffff0e00 ffff802b",
      "MWROM      0b00 ffffffff ffffffff",
      "MINI       0925 ffff0e00 ffff802b",
      "VIEWF      0591 ffff0e00 ffff802b",
      "French     055b ffff0e00 ffff802b",
      "German     069f ffff0e00 ffff802b",
      "Spanish    07ba ffff0e00 ffff802b",
      "Turkish    0667 ffff0e00 ffff802b",
      "Greek      0ca0 ffff0e00 ffff802b",
      "MATT3      2d4c ffff0e00 ffff802b",
      "MAT        06ea 00007500 00007500",
      "S          0acf ffff0e00 ffff802b",
      "FILES41    138f ffff0e00 ffff802b",
      "UASS2-1    04d7 ffff0e00 ffff802b",
      "UASS2-2    074a ffff0e00 ffff802b",
      "UASS2-3    0627 ffff0e00 ffff802b",
      "WORDSOR    040f ffff0e00 ffff802b",
      "COMPARE    0c78 ffff0e00 ffff802b",
      "BIBLIO     08ee ffff0e00 ffff802b",
      "MSCN704    05e9 00004556 00024353",
      "MENU       1196 ffff0e00 ffff802b",
      "ASTAAD3    1a76 ffff0e00 ffff802b",
      "ASTAAD     1457 ffff0e00 ffff802b",
      "CHARTES    0b67 ffff0e00 ffff802b",
      "MOUSEPR    0bd6 ffff0e00 ffff802b",
      "TEST       0746 ffff0e00 ffff802b",
      "S-A        03a3 ffff0e00 ffff802b",
      "ST1        0401 ffff0e00 ffff802b",
      "ST2        021f ffff0e00 ffff802b",
      "ST3        02a3 ffff0e00 ffff802b",
      "SORTS      09f7 ffff0e00 ffff802b",
      "SQUEEZE    33b6 ffff0e00 ffff802b",
      "SQZOBJ     0b5e 00008000 00008000",
      "UASS3-1    0401 ffff0e00 ffff802b",
      "UASS3-2    0996 ffff0e00 ffff802b",
      "FILES51    1b4f ffff0e00 ffff802b",
      "EXAM       02cc 00000000 00000000",
      "VLIST      0290 ffff0e00 ffff802b",
      "HEAP       0705 ffff0e00 ffff802b",
      "ELEVEN     18ab ffff0e00 ffff802b",
      "BIBLIO     089f ffff0e00 ffff802b",
      "MSCN705    05a4 00014556 00020d73",
      "MENU       0fb6 ffff0e00 ffff802b",
      "ICHING2    1c62 ffff0e00 ffff802b",
      "RENUM      208f ffff0e00 ffff802b",
      "Robj       0585 00002fd8 00002fd8",
      "PATTERN    043d ffff0e00 ffff802b",
      "Patns      02df ffff0e00 ffff802b",
      "CALENDA    1bac ffff0e00 ffff802b",
      "AST        001a ffff0e00 ffff802b",
      "ASTAAD     4bd7 ffff0e00 ffff802b",
      "FILE       0331 ffff0e00 ffff802b",
      "FILES72    0331 ffff0e00 ffff802b",
      "BTHDAYS    036c 00000000 00000000",
      "UASS51     0231 ffff0e00 ffff802b",
      "UASS52     0597 ffff0e00 ffff802b",
      "UNICAL     0862 ffff0e00 ffff802b",
      "BIBLIO     089f ffff0e00 ffff802b",
      "MSCN707    06ce 00014556 0002616c",
      "DMENU      1d95 ffff0e00 ffff802b",
      "Centre     0f3b ffff1900 ffff8023",
      "person     0163 ffffffff ffffffff",
      "ZAP        1284 ffff0e00 ffff802b",
      "LOADER     06e6 ffff0e00 ffff802b",
      "EDIT       122b ffff0e00 ffff802b",
      "wkfile     0037 00000000 ffffffff",
      "HELPSHE    03fb ffffffff ffffffff",
      "MENU       0291 ffff0e00 ffff802b",
      "PRINT      0887 ffff0e00 ffff802b",
      "MANAGER    0279 00008000 00008000",
      "SMANAGE    0dac ffff0e00 ffff802b",
      "SW-TEST    0130 ffffffff ffffffff",
      "CLOCK      2808 ffff0e00 ffff802b",
      "STEAMS     192a ffff0e00 ffff802b",
      "GRAPH7     051c ffff0e00 ffff802b",
      "FILES      003a ffff0e00 ffff802b",
      "FileTxt    02a0 00000000 ffffffff",
      "FILES86    274a ffff0e00 ffff802b",
      "ORDERS     0420 00000000 00000000",
      "ITEMS      02f4 00000000 00000000",
      "DRAUGHT    1e9d ffff0e00 ffff802b",
      "BIBLIO     089f ffff0e00 ffff802b",
      "MSCN708    0568 00000000 ffffffff",
      "MENU       23bc ffff0e00 ffff802b",
      "3DLands    0dc3 ffff0e00 ffff802b",
      "3DLandB    0d39 ffff0e00 ffff802b",
      "PAGER1     0601 ffff0e00 ffff802b",
      "Pager2     1884 ffff0e00 ffff802b",
      "DualCol    1700 ffff0e00 ffff802b",
      "Centre     0f3b ffff0e00 ffff8023",
      "DISPLAY    11fa ffff0e00 ffff802b",
      "Person1    0163 ffffffff ffffffff",
      "Person2    0163 ffffffff ffffffff",
      "AWIPE      0b10 ffff0e00 ffff802b",
      "FP         09fe ffff0e00 ffff802b",
      "FPcode     0428 00006000 00006000",
      "FPsrc      1442 ffff0e00 ffff802b",
      "Dater      06b5 ffff0e00 ffff802b",
      "DATCON     09d3 ffff0e00 ffff802b",
      "Time1      012b ffff0e00 ffff802b",
      "Rally1     1504 ffff0e00 ffff802b",
      "Coaster    0de9 ffff0e00 ffff802b",
      "DATA       1900 00000e00 00000e00",
      "BIBLIO     089f ffff0e00 ffff802b",
      "MSCN709    0b2e 00000000 ffffffff",
      "WordWra    00c0 ffff0e00 ffff802b",
      "Menu       21e0 ffff0e00 ffff802b",
      "Share      1b04 ffff0e00 ffff802b",
      "Dis        04eb ffff0e00 ffff802b",
      "Dis1       3533 ffff0e00 ffff802b",
      "Dis2       2cc0 ffff0e00 ffff802b",
      "FileByt    0246 ffff0e00 ffff802b",
      "Crib1      0373 ffff0e00 ffff802b",
      "Crib2      1e71 ffff0e00 ffff802b",
      "Edit       1ddd ffff0e00 ffff802b",
      "DROM       0e2a ffff0e00 ffff802b",
      "Tele3-5    0427 ffff0e00 ffff802b",
      "Backup     09bf ffff0e00 ffff802b",
      "VocBoot    056a ffff0e00 ffff802b",
      "French     055b ffff0e00 ffff802b",
      "VocTest    0c08 ffff0e00 ffff802b",
      "German     069f ffff0e00 ffff802b",
      "Greek      0ca0 ffff0e00 ffff802b",
      "Turkish    0667 ffff0e00 ffff802b",
      "EFRVOCA    0115 00006576 0002462e",
      "EINSTRU    005a 00006576 0002492e",
      "FFRVOCA    0115 00006576 0002462e",
      "FINSTRU    005a 00006576 0002492e",
      "Clock      06c2 ffff0e00 ffff802b",
      "Biblio     089f ffff0e00 ffff802b",
      "MSCN802    0a4b 00000000 ffffffff",
      "Menu       2164 ffff0e00 ffff802b",
      "InpDemo    0d8c ffff0e00 ffff802b",
      "EdLine     047c 00003b00 00003b00",
      "EditLn     1fdd ffff0e00 ffff802b",
      "SetUp      09b2 ffff0e00 ffff802b",
      "Design     1a24 ffff1800 ffff802b",
      "TForm      1dfc ffff0e00 ffff802b",
      "TTxDemo    0701 ffff0e00 ffff802b",
      "Ask        093b ffff0e00 ffff802b",
      "Info       2acf 00000000 ffffffff",
      "ASKROM     4000 00008000 00008000",
      "AcesHi     1852 ffff0e00 ffff802b",
      "Split      01ca ffff0e00 ffff802b",
      "Digit      0d0e ffff0e00 ffff802b",
      "FindEXE    2e00 00000000 ffffffff",
      "HowUse     0c9e 00000000 ffffffff",
      "Editor     0bc7 ffff0e00 ffff802b",
      "Edit2      356d ffff0e00 ffff802b",
      "EditTxt    3afe 00000000 ffffffff",
      "Biblio     089f ffff0e00 ffff802b",
      "MSCN804    091f 00000000 ffffffff",
      "Menu       24c7 ffff0e00 ffff802b",
      "GrabIt     0941 ffff0e00 ffff802b",
      "BRIC       0dce ffff0e00 ffff802b",
      "TestB      037b ffff0e00 ffff802b",
      "GrPlot     0679 ffff0e00 ffff802b",
      "GrPlot1    16bc ffff0e00 ffff802b",
      "WotDFS     18b5 ffff0e00 ffff802b",
      "ErrMenu    01e0 ffff0e00 ffff802b",
      "Err1       0083 ffff0e00 ffff802b",
      "Err2       052d ffff0e00 ffff802b",
      "Err3       0094 ffff0e00 ffff802b",
      "Err4       070c ffff0e00 ffff802b",
      "BProg2     08a5 ffff0e00 ffff802b",
      "PlusMin    0523 ffff0e00 ffff802b",
      "PSDump     0b90 ffff0e00 ffff802b",
      "Mo2Dump    5000 00003000 00000000",
      "Cls0Bug    0126 ffff0e00 ffff802b",
      "Prog1      0133 ffff0e00 ffff802b",
      "Prog2      01e7 ffff0e00 ffff802b",
      "Printer    04e9 ffff0e00 ffff802b",
      "Caller     0550 ffff0e00 ffff802b",
      "Clowns     09a6 ffff0e00 ffff802b",
      "Crazy      0c40 00000000 ffffffff",
      "Biblio     089f ffff0e00 ffff802b",
      "MSCN806    09e4 00000000 ffffffff",
      "Menu       242c ffff0e00 ffff802b",
      "Spline1    05d5 ffff0e42 0000edf7",
      "Spline2    14c0 ffff0e42 00003ea1",
      "Turmite    0b96 ffff0e00 ffff802b",
      "SWRVar     08df ffff0e00 ffff802b",
      "SWRProc    08a3 ffff0e00 ffff802b",
      "Fog        0f59 ffff0e42 0001024a",
      "FogTest    02d5 00000000 ffffffff",
      "CharEd     18a5 ffff0e00 ffff802b",
      "Example    02d0 00000e00 00000e00",
      "Squeeze    15d3 ffff0e00 ffff802b",
      "FCMenu     093b ffff0e00 ffff802b",
      "ScrnAsm    04ab ffff0e00 ffff80e7",
      "Loader     0b70 ffff0e42 0001985d",
      "WWSave     07c6 00000000 ffffffff",
      "List1      019d ffff0100 ffff5300",
      "List2      0159 ffff0100 ffff5300",
      "List3      0127 ffff0100 ffff5300",
      "List4      00cd ffff0100 ffff5300",
      "List5      041c ffff0100 ffff5300",
      "Biblio     089f ffff0e00 ffff802b",
      "MSCN902    0a3f 00000000 ffffffff",
      "Solit1     0edb ffff0e00 ffff802b",
      "Solit2     1217 ffff0e00 ffff80e7"
      */
      "0:00:22 MENU       0f6f ffff1900 ffff802b",
      "0:01:23 STATUS     0878 ffff1f00 ffff1f00",
      "0:02:02 SCOPE      13a7 ffff1f00 ffff802b",
      "0:03:18 COMPARE    1337 ffff1f00 ffff1f00",
      "0:04:33 FLEXDMP    173c ffff1f00 ffff1f00",
      "0:06:00 INKEY      022d ffff1f00 ffff802b",
      "0:06:19 ADFSMEN    14fc ffff1f00 ffff802b",
      "0:07:39 BBDATE     15a1 ffff1f00 ffff1f00",
      "0:09:01 BBDATE1    051b ffff1f00 ffff802b",
      "0:09:30 BBDATE2    0440 ffff1f00 ffff802b",
      "0:09:56 FUNMEN     0209 ffff1f00 ffff802b",
      "0:10:16 FILERA     1d3f ffff1f00 ffff802b",
      "0:12:04 BANK       1b90 00000000 00000000",
      "0:13:45 P.BANK     036c 00000000 00000000",
      "0:14:09 TUTAN      1d68 ffff1f00 ffff802b",
      "0:15:56 BIBLIO     078d ffff1f00 ffff802b",
      "0:16:33 MSCN505    07b2 00003000 00003000",
      "0:17:09 FORUM      02f4 ffff1f00 ffff1f00",
      "0:18:48 MENU       0f8f ffff1900 ffff8023",
      "0:19:50 USIDEMO    091f ffff1f00 ffff8023",
      "0:20:33 M.USI      04a4 00001300 00001300",
      "0:20:59 USI        2057 ffff1f00 ffff8023",
      "0:22:55 PRESSC     04d1 ffff1f00 ffff8023",
      "0:23:22 RAMSAFE    19d3 ffff1f00 ffff8023",
      "0:24:58 TIMER      0d78 ffff1900 ffff8023",
      "0:25:54 COMPILE    33a6 ffff1900 ffff8023",
      "0:28:55 EVAL2A     02ea ffff0e00 ffff802b",
      "0:29:15 EVAL3      02cf ffff1900 ffff8023",
      "0:29:35 EVAL4      0388 ffff1900 ffff8023",
      "0:29:58 BURGER     1d2b ffff1900 ffff8023",
      "0:31:46 BIBLIO     064f 00000800 00008023",
      "0:32:18 MSCN409    0717 ffffffff ffffffff",
      "0:32:54 BJACK      0500 00007000 0000740f",
      "0:33:22 BJACK2     3200 00002000 00003d00",
      "0:37:20 MENU       1273 ffff0e00 ffff802b",
      "0:38:31 CARPARK    12ff ffff0e00 ffff802b",
      "0:39:45 MULTCOL    0ef1 ffff0e00 ffff802b",
      "0:40:45 CREATE     02b4 ffff0e00 ffff802b",
      "0:41:05 DISPLAY    0227 ffff0e00 ffff802b",
      "0:41:25 UPDATE     02c8 ffff0e00 ffff802b",
      "0:41:44 Dates      0046 ffffffff ffffffff",
      "0:41:57 NOTE       0237 ffff0e00 ffff802b",
      "0:42:16 EDITOR     19a2 00000000 ffffffff",
      "0:43:51 FAPPEND    020a 00000000 ffffffff",
      "0:44:11 MWSOURC    3565 ffff0e00 ffff802b",
      "0:47:18 MWrom      0afb 00008000 00008000",
      "0:48:05 DSPOOL     0b25 ffff0e00 ffff802b",
      "0:48:53 DISCSPL    00f8 00000900 00000900",
      "0:49:07 STRSORT    0275 ffff0e00 ffff802b",
      "0:49:26 PASSWOR    03b6 ffff0e00 ffff802b",
      "0:49:49 FUNCTIO    0190 ffff0e00 ffff802b",
      "0:50:06 VECTOR1    0345 ffff0e00 ffff802b",
      "0:50:29 VECTOR2    0e33 ffff0e00 ffff802b",
      "0:51:28 COMMAS     0434 ffff0e00 ffff802b",
      "0:51:53 MF-EXAM    079c ffff0e00 ffff802b",
      "0:52:30 ASSEM1     0311 ffff0e00 ffff802b",
      "0:52:51 ASSEM2     09e6 ffff0e00 ffff802b",
      "0:53:36 BBC-IBM    1a76 ffff0e00 ffff802b",
      "0:55:15 BIBLIO     08ee ffff0e00 ffff802b",
      "0:55:55 MSCN701    07a0 00004556 00024353",
      "0:56:33 VOL6       57f1 ffffffff ffffffff",
      "1:01:52 MENU       107f ffff0e00 ffff802b",
      "1:02:55 XW         1452 ffff1400 ffff802b",
      "1:04:11 PROG1      059a ffff0e00 ffff802b",
      "1:04:39 PROG2      0960 ffff0e00 ffff802b",
      "1:05:20 WORM       14ec ffff0e00 ffff802b",
      "1:06:37 MWPROG     0310 ffff0e00 ffff802b",
      "1:06:57 MWROM      0b00 ffffffff ffffffff",
      "1:07:43 MINI       0925 ffff0e00 ffff802b",
      "1:08:23 VIEWF      0591 ffff0e00 ffff802b",
      "1:08:51 French     055b ffff0e00 ffff802b",
      "1:09:18 German     069f ffff0e00 ffff802b",
      "1:09:50 Spanish    07ba ffff0e00 ffff802b",
      "1:10:25 Turkish    0667 ffff0e00 ffff802b",
      "1:10:56 Greek      0ca0 ffff0e00 ffff802b",
      "1:11:47 MATT3      2d4c ffff0e00 ffff802b",
      "1:14:25 MAT        06ea 00007500 00007500",
      "1:14:57 S          0acf ffff0e00 ffff802b",
      "1:15:41 FILES41    138f ffff0e00 ffff802b",
      "1:16:55 UASS2-1    04d7 ffff0e00 ffff802b",
      "1:17:21 UASS2-2    074a ffff0e00 ffff802b",
      "1:17:55 UASS2-3    0627 ffff0e00 ffff802b",
      "1:18:25 WORDSOR    040f ffff0e00 ffff802b",
      "1:18:49 COMPARE    0c78 ffff0e00 ffff802b",
      "1:19:40 BIBLIO     08ee ffff0e00 ffff802b",
      "1:20:19 MSCN704    05e9 00004556 00024353",
      "1:21:21 MENU       1196 ffff0e00 ffff802b",
      "1:22:27 ASTAAD3    1a76 ffff0e00 ffff802b",
      "1:24:04 ASTAAD     1457 ffff0e00 ffff802b",
      "1:25:21 CHARTES    0b67 ffff0e00 ffff802b",
      "1:26:08 MOUSEPR    0bd6 ffff0e00 ffff802b",
      "1:26:56 TEST       0746 ffff0e00 ffff802b",
      "1:27:30 S-A        03a3 ffff0e00 ffff802b",
      "1:27:52 ST1        0401 ffff0e00 ffff802b",
      "1:28:16 ST2        021f ffff0e00 ffff802b",
      "1:28:33 ST3        02a3 ffff0e00 ffff802b",
      "1:28:51 SORTS      09f7 ffff0e00 ffff802b",
      "1:29:33 SQUEEZE    33b6 ffff0e00 ffff802b",
      "1:32:33 SQZOBJ     0b5e 00008000 00008000",
      "1:33:20 UASS3-1    0401 ffff0e00 ffff802b",
      "1:33:43 UASS3-2    0996 ffff0e00 ffff802b",
      "1:34:25 FILES51    1b4f ffff0e00 ffff802b",
      "1:36:05 EXAM       02cc 00000000 00000000",
      "1:36:23 VLIST      0290 ffff0e00 ffff802b",
      "1:36:42 HEAP       0705 ffff0e00 ffff802b",
      "1:37:15 ELEVEN     18ab ffff0e00 ffff802b",
      "1:38:45 BIBLIO     089f ffff0e00 ffff802b",
      "1:39:23 MSCN705    05a4 00014556 00020d73",
      "1:40:34 MENU       0fb6 ffff0e00 ffff802b",
      "1:41:34 ICHING2    1c62 ffff0e00 ffff802b",
      "1:43:17 RENUM      208f ffff0e00 ffff802b",
      "1:45:13 Robj       0585 00002fd8 00002fd8",
      "1:45:41 PATTERN    043d ffff0e00 ffff802b",
      "1:46:05 Patns      02df ffff0e00 ffff802b",
      "1:46:24 CALENDA    1bac ffff0e00 ffff802b",
      "1:48:05 AST        001a ffff0e00 ffff802b",
      "1:48:16 ASTAAD     4bd7 ffff0e00 ffff802b",
      "1:52:32 FILE       0331 ffff0e00 ffff802b",
      "1:52:53 FILES72    0331 ffff0e00 ffff802b",
      "1:53:14 BTHDAYS    036c 00000000 00000000",
      "1:53:35 UASS51     0231 ffff0e00 ffff802b",
      "1:53:52 UASS52     0597 ffff0e00 ffff802b",
      "1:54:21 UNICAL     0862 ffff0e00 ffff802b",
      "1:54:58 BIBLIO     089f ffff0e00 ffff802b",
      "1:55:36 MSCN707    06ce 00014556 0002616c",
      "1:56:35 DMENU      1d95 ffff0e00 ffff802b",
      "1:58:21 Centre     0f3b ffff1900 ffff8023",
      "1:59:21 person     0163 ffffffff ffffffff",
      "1:59:36 ZAP        1284 ffff0e00 ffff802b",
      "2:00:46 LOADER     06e6 ffff0e00 ffff802b",
      "2:01:18 EDIT       122b ffff0e00 ffff802b",
      "2:02:27 wkfile     0037 00000000 ffffffff",
      "2:02:38 HELPSHE    03fb ffffffff ffffffff",
      "2:03:00 MENU       0291 ffff0e00 ffff802b",
      "2:03:19 PRINT      0887 ffff0e00 ffff802b",
      "2:03:57 MANAGER    0279 00008000 00008000",
      "2:04:15 SMANAGE    0dac ffff0e00 ffff802b",
      "2:05:09 SW-TEST    0130 ffffffff ffffffff",
      "2:05:24 CLOCK      2808 ffff0e00 ffff802b",
      "2:07:45 STEAMS     192a ffff0e00 ffff802b",
      "2:09:17 GRAPH7     051c ffff0e00 ffff802b",
      "2:09:44 FILES      003a ffff0e00 ffff802b",
      "2:09:55 FileTxt    02a0 00000000 ffffffff",
      "2:10:14 FILES86    274a ffff0e00 ffff802b",
      "2:12:33 ORDERS     0420 00000000 00000000",
      "2:12:56 ITEMS      02f4 00000000 00000000",
      "2:13:16 DRAUGHT    1e9d ffff0e00 ffff802b",
      "2:15:06 BIBLIO     089f ffff0e00 ffff802b",
      "2:15:44 MSCN708    0568 00000000 ffffffff",
      "2:16:58 MENU       23bc ffff0e00 ffff802b",
      "2:19:04 3DLands    0dc3 ffff0e00 ffff802b",
      "2:19:59 3DLandB    0d39 ffff0e00 ffff802b",
      "2:20:52 PAGER1     0601 ffff0e00 ffff802b",
      "2:21:22 Pager2     1884 ffff0e00 ffff802b",
      "2:22:52 DualCol    1700 ffff0e00 ffff802b",
      "2:24:17 Centre     0f3b ffff0e00 ffff8023",
      "2:25:17 DISPLAY    11fa ffff0e00 ffff802b",
      "2:26:25 Person1    0163 ffffffff ffffffff",
      "2:26:40 Person2    0163 ffffffff ffffffff",
      "2:26:55 AWIPE      0b10 ffff0e00 ffff802b",
      "2:27:41 FP         09fe ffff0e00 ffff802b",
      "2:28:23 FPcode     0428 00006000 00006000",
      "2:28:47 FPsrc      1442 ffff0e00 ffff802b",
      "2:30:03 Dater      06b5 ffff0e00 ffff802b",
      "2:30:35 DATCON     09d3 ffff0e00 ffff802b",
      "2:31:16 Time1      012b ffff0e00 ffff802b",
      "2:31:31 Rally1     1504 ffff0e00 ffff802b",
      "2:32:50 Coaster    0de9 ffff0e00 ffff802b",
      "2:33:45 DATA       1900 00000e00 00000e00",
      "2:35:15 BIBLIO     089f ffff0e00 ffff802b",
      "2:35:53 MSCN709    0b2e 00000000 ffffffff",
      "2:36:40 WordWra    00c0 ffff0e00 ffff802b",
      "2:37:31 Menu       21e0 ffff0e00 ffff802b",
      "2:39:30 Share      1b04 ffff0e00 ffff802b",
      "2:41:09 Dis        04eb ffff0e00 ffff802b",
      "2:41:34 Dis1       3533 ffff0e00 ffff802b",
      "2:44:37 Dis2       2cc0 ffff0e00 ffff802b",
      "2:47:11 FileByt    0246 ffff0e00 ffff802b",
      "2:47:29 Crib1      0373 ffff0e00 ffff802b",
      "2:47:50 Crib2      1e71 ffff0e00 ffff802b",
      "2:49:40 Edit       1ddd ffff0e00 ffff802b",
      "2:51:26 DROM       0e2a ffff0e00 ffff802b",
      "2:52:22 Tele3-5    0427 ffff0e00 ffff802b",
      "2:52:46 Backup     09bf ffff0e00 ffff802b",
      "2:53:27 VocBoot    056a ffff0e00 ffff802b",
      "2:53:55 French     055b ffff0e00 ffff802b",
      "2:54:23 VocTest    0c08 ffff0e00 ffff802b",
      "2:55:13 German     069f ffff0e00 ffff802b",
      "2:55:44 Greek      0ca0 ffff0e00 ffff802b",
      "2:56:35 Turkish    0667 ffff0e00 ffff802b",
      "2:57:07 EFRVOCA    0115 00006576 0002462e",
      "2:57:21 EINSTRU    005a 00006576 0002492e",
      "2:57:32 FFRVOCA    0115 00006576 0002462e",
      "2:57:46 FINSTRU    005a 00006576 0002492e",
      "2:57:57 Clock      06c2 ffff0e00 ffff802b",
      "2:58:29 Biblio     089f ffff0e00 ffff802b",
      "2:59:07 MSCN802    0a4b 00000000 ffffffff",
      "3:00:32 Menu       2164 ffff0e00 ffff802b",
      "3:02:31 InpDemo    0d8c ffff0e00 ffff802b",
      "3:03:25 EdLine     047c 00003b00 00003b00",
      "3:03:50 EditLn     1fdd ffff0e00 ffff802b",
      "3:05:43 SetUp      09b2 ffff0e00 ffff802b",
      "3:06:24 Design     1a24 ffff1800 ffff802b",
      "3:08:00 TForm      1dfc ffff0e00 ffff802b",
      "3:09:47 TTxDemo    0701 ffff0e00 ffff802b",
      "3:10:20 Ask        093b ffff0e00 ffff802b",
      "3:11:01 Info       2acf 00000000 ffffffff",
      "3:13:29 ASKROM     4000 00008000 00008000",
      "3:17:07 AcesHi     1852 ffff0e00 ffff802b",
      "3:18:36 Split      01ca ffff0e00 ffff802b",
      "3:18:51 Digit      0d0e ffff0e00 ffff802b",
      "3:19:45 FindEXE    2e00 00000000 ffffffff",
      "3:22:25 HowUse     0c9e 00000000 ffffffff",
      "3:23:16 Editor     0bc7 ffff0e00 ffff802b",
      "3:24:04 Edit2      356d ffff0e00 ffff802b",
      "3:27:08 EditTxt    3afe 00000000 ffffffff",
      "3:30:30 Biblio     089f ffff0e00 ffff802b",
      "3:31:08 MSCN804    091f 00000000 ffffffff",
      "3:32:35 Menu       24c7 ffff0e00 ffff802b",
      "3:34:43 GrabIt     0941 ffff0e00 ffff802b",
      "3:35:24 BRIC       0dce ffff0e00 ffff802b",
      "3:36:18 TestB      037b ffff0e00 ffff802b",
      "3:36:40 GrPlot     0679 ffff0e00 ffff802b",
      "3:37:11 GrPlot1    16bc ffff0e00 ffff802b",
      "3:38:35 WotDFS     18b5 ffff0e00 ffff802b",
      "3:40:05 ErrMenu    01e0 ffff0e00 ffff802b",
      "3:40:21 Err1       0083 ffff0e00 ffff802b",
      "3:40:33 Err2       052d ffff0e00 ffff802b",
      "3:41:00 Err3       0094 ffff0e00 ffff802b",
      "3:41:12 Err4       070c ffff0e00 ffff802b",
      "3:41:45 BProg2     08a5 ffff0e00 ffff802b",
      "3:42:23 PlusMin    0523 ffff0e00 ffff802b",
      "3:42:50 PSDump     0b90 ffff0e00 ffff802b",
      "3:43:38 Mo2Dump    5000 00003000 00000000",
      "3:48:09 Cls0Bug    0126 ffff0e00 ffff802b",
      "3:48:24 Prog1      0133 ffff0e00 ffff802b",
      "3:48:38 Prog2      01e7 ffff0e00 ffff802b",
      "3:48:54 Printer    04e9 ffff0e00 ffff802b",
      "3:49:19 Caller     0550 ffff0e00 ffff802b",
      "3:49:47 Clowns     09a6 ffff0e00 ffff802b",
      "3:50:28 Crazy      0c40 00000000 ffffffff",
      "3:51:18 Biblio     089f ffff0e00 ffff802b",
      "3:51:56 MSCN806    09e4 00000000 ffffffff",
      "3:53:21 Menu       242c ffff0e00 ffff802b",
      "3:55:29 Spline1    05d5 ffff0e42 0000edf7",
      "3:55:57 Spline2    14c0 ffff0e42 00003ea1",
      "3:57:15 Turmite    0b96 ffff0e00 ffff802b",
      "3:58:03 SWRVar     08df ffff0e00 ffff802b",
      "3:58:42 SWRProc    08a3 ffff0e00 ffff802b",
      "3:59:20 Fog        0f59 ffff0e42 0001024a",
      "4:00:20 FogTest    02d5 00000000 ffffffff",
      "4:00:39 CharEd     18a5 ffff0e00 ffff802b",
      "4:02:09 Example    02d0 00000e00 00000e00",
      "4:02:43 Squeeze    15d3 ffff0e00 ffff802b",
      "4:04:04 FCMenu     093b ffff0e00 ffff802b",
      "4:04:45 ScrnAsm    04ab ffff0e00 ffff80e7",
      "4:05:10 Loader     0b70 ffff0e42 0001985d",
      "4:05:57 WWSave     07c6 00000000 ffffffff",
      "4:06:32 List1      019d ffff0100 ffff5300",
      "4:06:48 List2      0159 ffff0100 ffff5300",
      "4:07:02 List3      0127 ffff0100 ffff5300",
      "4:07:16 List4      00cd ffff0100 ffff5300",
      "4:07:29 List5      041c ffff0100 ffff5300",
      "4:07:53 Biblio     089f ffff0e00 ffff802b",
      "4:08:31 MSCN902    0a3f 00000000 ffffffff",
      "4:09:14 Solit1     0edb ffff0e00 ffff802b",
      "4:10:12 Solit2     1217 ffff0e00 ffff80e7",
    );
  }

  function transpo_hexdump($mem) {
    if (!isset($mem) || $mem==="") { return; }
    $l=strlen($mem);
    if (defined("HEXDUMP_MAX") && $l>HEXDUMP_MAX)
      $l=HEXDUMP_MAX;
    $sbuf="";
    for ($i=0;$i<$l;$i++) {
      if (!($i%16)) { printf ("%04x   ", $i); }
      printf ("%02x ", $z=ord($mem[$i]));
      if (!ctype_print($w=$mem[$i])||$z>127||$w==="\r"||$w==="\n") {
        $sbuf.=".";
      } else {
        $sbuf.=$w;
      }
      if ($i%16 == 15) {
        print "   ".$sbuf."\n";
        $sbuf="";
      }
    }
    if ($i%16<15) {
      for ($i=(16-$i%16);$i;$i--) {
        print "   ";
      }
      print "   ".$sbuf."\n";
    }
    //if ($i%16 != 15) { print "\n"; }
  }


?>
