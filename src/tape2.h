#ifndef __INC_TAPE2_H
#define __INC_TAPE2_H


/* BUILD OPTIONS */
#define BUILD_TAPE_SANITY
#define BUILD_TAPE_DEV_MENU
#define BUILD_TAPE_MENU_GREYOUT_CAT
#define BUILD_TAPE_PRINT_FRAMING_CHANGES
#define BUILD_TAPE_READ_POS_TO_EOF_ON_WRITE
#define BUILD_TAPE_INCOMPATIBLE_NEW_SAVE_STATES
/*#define BUILD_TAPE_CHECK_POLL_TIMING*/
/*#define BUILD_TAPE_LOOP */
#define BUILD_TAPE_NO_FASTTAPE_VIDEO_HACKS /* removes strange fasttape frameskip thing in video code */

/* include this line to introduce a bug which produces
   a non-uniform TDRE delay distribution (rather than
   the broadly flat one which actually occurs on h/w)
   -- this is intended purely for testing the tests */
/*#define BUILD_TAPE_BUGGY_LUMPY_TDRE_DISTRIBUTION*/

#define TAPE_E_OK                               0
#define TAPE_E_MALLOC                           1
#define TAPE_E_EOF                              2
#define TAPE_E_BUG                              3
#define TAPE_E_FOPEN                            4
#define TAPE_E_FTELL                            5
#define TAPE_E_FREAD                            6
#define TAPE_E_FILE_TOO_LARGE                   7
#define TAPE_E_ZLIB_INIT                        8
#define TAPE_E_ZLIB_DECOMPRESS                  9
#define TAPE_E_DECOMPRESSED_TOO_LARGE          10
#define TAPE_E_SAVESTATE                       11
#define TAPE_E_LOADSTATE                       12

#define TAPE_E_CSW_BAD_MAGIC                  101
#define TAPE_E_CSW_BAD_VERSION                102
#define TAPE_E_CSW_BAD_RATE                   103
#define TAPE_E_CSW_HEADER_NUM_PULSES          104
#define TAPE_E_CSW_BODY_LARGE                 106
#define TAPE_E_CSW_COMP_VALUE                 107
#define TAPE_E_CSW_BAD_FLAGS                  108
#define TAPE_E_CSW_PULSES_MISMATCH            109
#define TAPE_E_CSW_HEADER_TRUNCATED           110
#define TAPE_E_CSW_WRITE_NULL_PULSE           111 /* TOHv3   */
#define TAPE_E_CSW_LONGPULSE_UNDER_256        112 /* TOHv3.2 */

#define TAPE_E_TIBET_BADCHAR                  200
#define TAPE_E_TIBET_VERSION                  201
#define TAPE_E_TIBET_VERSION_LINE             202
#define TAPE_E_TIBET_VERSION_LINE_NOSPC       203
#define TAPE_E_TIBET_UNK_WORD                 204
#define TAPE_E_TIBET_VERSION_MAJOR            205
#define TAPE_E_TIBET_TOO_MANY_SPANS           206 /* not probed by tests */
#define TAPE_E_TIBET_FIELD_INCOMPAT           207
#define TAPE_E_TIBET_MULTI_DECIMAL_POINT      208
#define TAPE_E_TIBET_POINT_ENDS_DECIMAL       209
#define TAPE_E_TIBET_DECIMAL_BAD_CHAR         210
#define TAPE_E_TIBET_DECIMAL_TOO_LONG         211
#define TAPE_E_TIBET_DECIMAL_PARSE            212 /* not probed by tests */
/*#define TAPE_E_TIBET_SHORT_SILENCE            213 */ /* (relaxed: no lower bound on silence duration) */
#define TAPE_E_TIBET_LONG_SILENCE             214
#define TAPE_E_TIBET_INT_TOO_LONG             215
#define TAPE_E_TIBET_INT_PARSE                217
#define TAPE_E_TIBET_INT_BAD_CHAR             218
#define TAPE_E_TIBET_LONG_LEADER              219
#define TAPE_E_TIBET_DUP_BAUD                 220
#define TAPE_E_TIBET_BAD_FRAMING              221
#define TAPE_E_TIBET_DUP_FRAMING              222
#define TAPE_E_TIBET_DUP_TIME                 223
#define TAPE_E_TIBET_TIME_HINT_TOOLARGE       224
#define TAPE_E_TIBET_BAD_BAUD                 225
#define TAPE_E_TIBET_DUP_PHASE                226
#define TAPE_E_TIBET_BAD_PHASE                227
#define TAPE_E_TIBET_DUP_SPEED                228
#define TAPE_E_TIBET_SPEED_HINT_HIGH          229
#define TAPE_E_TIBET_SPEED_HINT_LOW           230
#define TAPE_E_TIBET_DATA_JUNK_FOLLOWS_START  231
#define TAPE_E_TIBET_DATA_JUNK_FOLLOWS_LINE   232
#define TAPE_E_TIBET_DATA_ILLEGAL_CHAR        233
#define TAPE_E_TIBET_DATA_DOUBLE_PULSE        234
#define TAPE_E_TIBET_DATA_EXCESSIVE_TONES     235 /* not probed by tests */
#define TAPE_E_TIBET_DANGLING_TIME            236
#define TAPE_E_TIBET_DANGLING_PHASE           237
#define TAPE_E_TIBET_DANGLING_SPEED           238
#define TAPE_E_TIBET_DANGLING_BAUD            239
#define TAPE_E_TIBET_DANGLING_FRAMING         240
#define TAPE_E_TIBET_NO_DECODE                241
#define TAPE_E_TIBET_EMPTY_LEADER             242 /* TOHv3 */
#define TAPE_E_TIBET_OP_LEN                   243 /* TOHv3: writing */
#define TAPE_E_TIBET_ABSENT_VERSION           244 /* TOHv3: caught by unit test! */
#define TAPE_E_TIBET_VERSION_NO_DP            245 /* TOHv3.2: version parsing    */
#define TAPE_E_TIBET_VERSION_NON_NUMERIC      246 /* TOHv3.2: version parsing    */
#define TAPE_E_TIBET_VERSION_BAD_LEN          247 /* TOHv3.2: version parsing    */
#define TAPE_E_TIBET_VERSION_MINOR            248 /* TOHv3.2: compatibility      */
#define TAPE_E_TIBET_CONCAT_VERSION_MISMATCH  249 /* TOHv3.2: force TIBET concat exact version match */

#define TAPE_E_UEF_BAD_MAGIC                  301
#define TAPE_E_UEF_BAD_HEADER                 302
#define TAPE_E_UEF_TRUNCATED                  303
#define TAPE_E_UEF_UNKNOWN_CHUNK              304
#define TAPE_E_UEF_OVERSIZED_CHUNK            305
#define TAPE_E_UEF_TOO_MANY_METADATA_CHUNKS   306
#define TAPE_E_UEF_0104_NUM_BITS              307
#define TAPE_E_UEF_0104_NUM_STOPS             308
#define TAPE_E_UEF_CHUNKLEN_0000              309
#define TAPE_E_UEF_CHUNKLEN_0001              310
#define TAPE_E_UEF_CHUNKLEN_0003              311
#define TAPE_E_UEF_CHUNKLEN_0005              312
#define TAPE_E_UEF_CHUNKLEN_0006              313
#define TAPE_E_UEF_CHUNKLEN_0007              314
#define TAPE_E_UEF_CHUNKLEN_0008              315
#define TAPE_E_UEF_CHUNKLEN_0009              316
#define TAPE_E_UEF_CHUNKLEN_000A              317
#define TAPE_E_UEF_CHUNKLEN_0100              318
#define TAPE_E_UEF_CHUNKLEN_0102              319
#define TAPE_E_UEF_CHUNKLEN_0104              320
#define TAPE_E_UEF_CHUNKLEN_0110              321
#define TAPE_E_UEF_CHUNKLEN_0111              322
#define TAPE_E_UEF_CHUNKLEN_0112              323
#define TAPE_E_UEF_CHUNKLEN_0115              324
#define TAPE_E_UEF_CHUNKLEN_0116              325
#define TAPE_E_UEF_CHUNKLEN_0113              326
#define TAPE_E_UEF_CHUNKLEN_0114              327
#define TAPE_E_UEF_CHUNKLEN_0117              328
#define TAPE_E_UEF_CHUNKLEN_0120              329
#define TAPE_E_UEF_CHUNKLEN_0130              330
#define TAPE_E_UEF_CHUNKLEN_0131              331
#define TAPE_E_UEF_CHUNKDAT_0005              332
#define TAPE_E_UEF_CHUNKDAT_0006              333
#define TAPE_E_UEF_0114_BAD_PULSEWAVE_1       334
#define TAPE_E_UEF_0114_BAD_PULSEWAVE_2       335
#define TAPE_E_UEF_0114_BAD_PULSEWAVE_COMBO   336
#define TAPE_E_UEF_CHUNK_SPENT                337
#define TAPE_E_UEF_0114_BAD_NUM_CYCS          338
#define TAPE_E_UEF_0116_NEGATIVE_GAP          339
#define TAPE_E_UEF_0116_HUGE_GAP              340
#define TAPE_E_UEF_0102_WEIRD_DATA_0          341
#define TAPE_E_UEF_EXCESS_0000                342
#define TAPE_E_UEF_EXCESS_0001                343
#define TAPE_E_UEF_EXCESS_0003                344
#define TAPE_E_UEF_EXCESS_0005                345
#define TAPE_E_UEF_EXCESS_0008                346
#define TAPE_E_UEF_0130_VOCAB                 347
#define TAPE_E_UEF_0130_NUM_TAPES             348
#define TAPE_E_UEF_0130_NUM_CHANNELS          349
#define TAPE_E_UEF_0131_TAPE_ID               350
#define TAPE_E_UEF_0131_CHANNEL_ID            351
#define TAPE_E_UEF_0131_TAPE_ID_130_LIMIT     352
#define TAPE_E_UEF_0131_CHANNEL_ID_130_LIMIT  353
#define TAPE_E_UEF_0131_DESCRIPTION_LONG      354
#define TAPE_E_UEF_LONG_CHUNK                 355 /* exceeds generic length limit */
#define TAPE_E_UEF_INLAY_SCAN_BPP             356
#define TAPE_E_UEF_INLAY_SCAN_ZERO            357
#define TAPE_E_UEF_0115_ILLEGAL               358
#define TAPE_E_UEF_0117_BAD_RATE              359
#define TAPE_E_UEF_UTF8_DEC_1                 360 /* UTF-8 decoding errors */
#define TAPE_E_UEF_UTF8_DEC_2                 361
#define TAPE_E_UEF_GLOBAL_CHUNK_SPAM          362 /* excessive number of "global" chunks (instructions, origin etc.) */
#define TAPE_E_UEF_SAVE_NONSTD_BAUD           363 /* UEF 0.10 disallows non-300, non-1200 baud rates in chunk 117 */

#define TAPE_E_SAVE_FOPEN                     400
#define TAPE_E_SAVE_FWRITE                    401
#define TAPE_E_SAVE_ZLIB_COMPRESS             402
#define TAPE_E_SAVE_ZLIB_INIT                 403

#define TAPE_E_EXPIRY                         404 /* TOHv4 */

#define TAPE_E_ACIA_TX_BITCLK_NOT_READY       500 /* not an error */

#endif /* __INC_TAPE2_H */
