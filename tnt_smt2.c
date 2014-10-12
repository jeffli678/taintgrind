#include "pub_tool_basics.h"
#include "pub_tool_hashtable.h"   // For tnt_include.h, VgHashtable
#include "pub_tool_libcassert.h"  // tl_assert
#include "pub_tool_libcbase.h"    // VG_strcpy
#include "pub_tool_libcprint.h"   // VG_(printf)
#include "pub_tool_machine.h"     // IRStmt
#include "pub_tool_tooliface.h"   // VG_(CallbackClosure)

#include "tnt_include.h"

char *TNT_(smt2_concat)( char *buf, ULong addr, UInt c );

const Int SMT2_ty[] = {
   0,
   1,
   8,
   16,
   32,
   64,
   128,  /* 128-bit scalar */
   32,   /* IEEE 754 float */
   64,   /* IEEE 754 double */
   32,   /* 32-bit Decimal floating point */
   64,   /* 64-bit Decimal floating point */
   128,  /* 128-bit Decimal floating point */
   128,  /* 128-bit floating point; implementation defined */
   128,  /* 128-bit SIMD */
   256   /* 256-bit SIMD */
};

// Array for tracking tmp variable types
UInt tt[TI_MAX];

void TNT_(smt2_preamble)()
{
    VG_(printf)("(set-logic QF_BV)\n");
}

char *TNT_(smt2_concat)( char *buf, ULong addr, UInt c )
{
   char tmp[1024];

   if ( c == 0 )
   {
      VG_(sprintf)(buf, "(concat a%llx a%llx)", addr, addr+1);
      return buf;
   }
   VG_(sprintf)(tmp, "(concat a%llx %s)", addr, TNT_(smt2_concat)(buf, addr+1, c-1) );
   VG_(strcpy)(buf, tmp);
   return buf;
}


// if tmp GOTO ...
void TNT_(smt2_exit) ( IRStmt *clone )
{
   IRExpr *guard = clone->Ist.Exit.guard;
   UInt gtmp     = guard->Iex.RdTmp.tmp;
   // Save current assertions
   VG_(printf)("(push)\n");
   // Invert branch
   if ( tv[gtmp] ) {
      VG_(printf)("(assert (= t%d_%d #b0))\n", gtmp, _ti(gtmp));
   } else {
      VG_(printf)("(assert (= t%d_%d #b1))\n", gtmp, _ti(gtmp));
   }
   VG_(printf)("(check-sat)\n(get-model)\n");
   // Restore assertions
   VG_(printf)("(pop)\n");
}


// ltmp = LOAD <ty> atmp
void TNT_(smt2_load_t) ( IRStmt *clone )
{

   UInt ltmp     = clone->Ist.WrTmp.tmp;
   UInt ty       = clone->Ist.WrTmp.data->Iex.Load.ty - Ity_INVALID;
   IRExpr* addr  = clone->Ist.WrTmp.data->Iex.Load.addr;
   UInt atmp     = addr->Iex.RdTmp.tmp;
   ULong address = tv[atmp];
   char buf[1024];

   if ( SMT2_ty[ty] == 32 )
   {
      VG_(printf)("(declare-fun t%d_%d () (_ BitVec %d))\n", ltmp, _ti(ltmp), SMT2_ty[ty]);
      //VG_(printf)("(assert (= t%d_%d (concat a%llx %s)))\n", ltmp, _ti(ltmp), address, TNT_(smt2_concat)(buf, address+1, 1) );
      VG_(printf)("(assert (= t%d_%d %s))\n", ltmp, _ti(ltmp), TNT_(smt2_concat)(buf, address, 2) );
      tt[ltmp] = SMT2_ty[ty];
   } else {
      VG_(printf)("SMT2_ty[ty] = %d\n", SMT2_ty[ty]);
      tl_assert(0);
   }
}

// STORE atmp = dtmp
void TNT_(smt2_store_tt) ( IRStmt *clone )
{

   IRExpr *addr = clone->Ist.Store.addr;
   IRExpr *data = clone->Ist.Store.data;
   UInt atmp    = addr->Iex.RdTmp.tmp;
   UInt dtmp    = data->Iex.RdTmp.tmp;
   ULong address = tv[atmp];
   int i;

   tl_assert(tt[dtmp]);

   int numbytes = tt[dtmp]/8;

   for ( i=0; i<numbytes; i++ )
   {
      VG_(printf)("(declare-fun a%llx () (_ BitVec 8))\n", address+i);
      VG_(printf)("(assert (= a%llx ((_ extract %d %d) t%d_%d)))\n", address+i, ((i+1)*8)-1, i*8, dtmp, _ti(dtmp) );
   }
}

#define smt2_sign_extend(a, b) \
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec " #b "))\n", ltmp, _ti(ltmp)); \
         VG_(printf)("(assert (= t%d_%d ((_ sign_extend " #a ") t%d_%d)))\n", ltmp, _ti(ltmp), rtmp, _ti(rtmp) ); \
         tt[ltmp] = b

#define smt2_zero_extend(a, b) \
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec " #b "))\n", ltmp, _ti(ltmp)); \
         VG_(printf)("(assert (= t%d_%d ((_ zero_extend " #a ") t%d_%d)))\n", ltmp, _ti(ltmp), rtmp, _ti(rtmp) ); \
         tt[ltmp] = b

#define smt2_extract(a, b, c) \
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec " #c "))\n", ltmp, _ti(ltmp)); \
         VG_(printf)("(assert (= t%d_%d ((_ extract " #b " " #a ") t%d_%d)))\n", ltmp, _ti(ltmp), rtmp, _ti(rtmp) ); \
         tt[ltmp] = c

// ltmp = <op> rtmp
void TNT_(smt2_unop_t) ( IRStmt *clone )
{

   UInt ltmp   = clone->Ist.WrTmp.tmp;
   UInt op     = clone->Ist.WrTmp.data->Iex.Unop.op;
   IRExpr* arg = clone->Ist.WrTmp.data->Iex.Unop.arg;
   UInt rtmp   = arg->Iex.RdTmp.tmp;

   switch(op) {
      case Iop_1Sto8:     smt2_sign_extend(7, 8);    break;
      case Iop_1Sto16:    smt2_sign_extend(15, 16);  break;
      case Iop_1Sto32:    smt2_sign_extend(31, 32);  break;
      case Iop_1Sto64:    smt2_sign_extend(63, 64);  break;
      case Iop_1Uto8:     smt2_zero_extend(7, 8);    break;
      case Iop_1Uto32:    smt2_zero_extend(31, 32);  break;
      case Iop_1Uto64:    smt2_zero_extend(63, 64);  break;
      case Iop_8Sto16:    smt2_sign_extend(8, 16);   break;
      case Iop_8Sto32:    smt2_sign_extend(24, 32);  break;
      case Iop_8Sto64:    smt2_sign_extend(56, 64);  break;
      case Iop_8Uto16:    smt2_zero_extend(8, 16);   break;
      case Iop_8Uto32:    smt2_zero_extend(24, 32);  break;
      case Iop_8Uto64:    smt2_zero_extend(56, 64);  break;
      case Iop_16to8:     smt2_extract(0, 7, 8);     break;
      case Iop_16HIto8:   smt2_extract(8, 15, 8);    break;
      case Iop_16Sto32:   smt2_sign_extend(16, 32);  break;
      case Iop_16Sto64:   smt2_sign_extend(48, 64);  break;
      case Iop_16Uto32:   smt2_zero_extend(16, 32);  break;
      case Iop_16Uto64:   smt2_zero_extend(48, 64);  break;
      case Iop_32to1:     smt2_extract(0, 0, 1);     break;
      case Iop_32to8:     smt2_extract(0, 7, 8);     break;
      case Iop_32to16:    smt2_extract(0, 15, 16);   break;
      case Iop_32HIto16:  smt2_extract(16, 31, 16);  break;
      case Iop_32Sto64:   smt2_sign_extend(32, 64);  break;
      case Iop_32Uto64:   smt2_zero_extend(32, 64);  break;
      case Iop_64to1:     smt2_extract(0, 0, 1);     break;
      case Iop_64to8:     smt2_extract(0, 7, 8);     break;
      case Iop_64to16:    smt2_extract(0, 15, 16);   break;
      case Iop_64to32:    smt2_extract(0, 31, 32);   break;
      case Iop_64HIto32:  smt2_extract(32, 63, 32);  break;
      case Iop_128to64:   smt2_extract(0, 63, 64);   break;
      case Iop_128HIto64: smt2_extract(64, 127, 64); break;
      default:
         VG_(printf)("smt2_unop_t %s\n", IROp_string[op-Iop_INVALID]);
         tl_assert(0);
   }
}


// ltmp = <op> rtmp1 rtmp2
void TNT_(smt2_binop_tc) ( IRStmt *clone )
{

   UInt ltmp    = clone->Ist.WrTmp.tmp;
   UInt op      = clone->Ist.WrTmp.data->Iex.Binop.op;
   IRExpr* arg1 = clone->Ist.WrTmp.data->Iex.Binop.arg1;
   IRExpr* arg2 = clone->Ist.WrTmp.data->Iex.Binop.arg2;
   UInt rtmp1   = arg1->Iex.RdTmp.tmp;
   UInt c;

   switch(op) {
      case Iop_Add32:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 32))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (bvadd t%d_%d #x%08x) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 32;
         break;
      case Iop_CmpEQ32:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 1))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (ite (= t%d_%d #x%08x) #b1 #b0) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 1;
         break;
      case Iop_CmpLE32S:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 1))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (ite (bvsle t%d_%d #x%08x) #b1 #b0) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 1;
         break;
      case Iop_CmpLE32U:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 1))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (ite (bvule t%d_%d #x%08x) #b1 #b0) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 1;
         break;
      case Iop_CmpLT32S:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 1))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (ite (bvslt t%d_%d #x%08x) #b1 #b0) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 1;
         break;
      case Iop_CmpLT32U:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 1))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (ite (bvult t%d_%d #x%08x) #b1 #b0) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 1;
         break;
      case Iop_Sub32:
         c = extract_IRConst( arg2->Iex.Const.con );
         tl_assert(tt[rtmp1] == 32);
         VG_(printf)("(declare-fun t%d_%d () (_ BitVec 32))\n", ltmp, _ti(ltmp));
         VG_(printf)("(assert (= t%d_%d (bvsub t%d_%d #x%08x) ))\n", ltmp, _ti(ltmp), rtmp1, _ti(rtmp1), c );
         tt[ltmp] = 32;
         break;
      default:
         VG_(printf)("smt2_binop_tc %s\n", IROp_string[op-Iop_INVALID]);
         tl_assert(0);
   }
}


// ltmp = rtmp
void TNT_(smt2_rdtmp) ( IRStmt *clone )
{

   UInt ltmp = clone->Ist.WrTmp.tmp;
   UInt rtmp = clone->Ist.WrTmp.data->Iex.RdTmp.tmp;

   tl_assert(tt[rtmp]);

   VG_(printf)("(declare-fun t%d_%d () (_ BitVec %d))\n", ltmp, _ti(ltmp), tt[rtmp]);
   VG_(printf)("(assert (= t%d_%d t%d_%d))\n", ltmp, _ti(ltmp), rtmp, _ti(rtmp) );
   tt[ltmp] = tt[rtmp];
}

// reg = tmp
void TNT_(smt2_put_t) ( IRStmt *clone )
{

   UInt reg     = clone->Ist.Put.offset;
   IRExpr *data = clone->Ist.Put.data;
   UInt tmp     = data->Iex.RdTmp.tmp;

   tl_assert(tt[tmp]);

   VG_(printf)("(declare-fun r%d_%d () (_ BitVec %d))\n", reg, ri[reg], tt[tmp]);
   VG_(printf)("(assert (= r%d_%d t%d_%d))\n", reg, ri[reg], tmp, _ti(tmp) );
}
