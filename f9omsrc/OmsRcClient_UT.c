﻿// \file f9omsrc/OmsRcClient_UT.c
// \author fonwinz@gmail.com
#define _CRT_SECURE_NO_WARNINGS
#include "f9omsrc/OmsRc.h"
#include "f9omstw/OmsToolsC.h" // f9omstw_IncStrAlpha();
#include "f9omstw/OmsMakeErrMsg.h"
#include "fon9/ConsoleIO.h"
#include "fon9/CTools.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

#ifdef fon9_WINDOWS
#include <crtdbg.h>
#endif
//--------------------------------------------------------------------------//
#define kMaxFieldCount  64
#define kMaxValueSize   64
typedef struct {
   // 為了簡化測試, 每個下單要求最多支援 kMaxFieldCount 個欄位, 每個欄位最多 kMaxValueSize bytes(包含EOS).
   char  Fields_[kMaxFieldCount][kMaxValueSize];
   char  RequestStr_[kMaxFieldCount * kMaxValueSize];
} RequestRec;

fon9_WARN_DISABLE_PADDING;
typedef struct {
   f9rc_ClientSession*           Session_;
   const f9OmsRc_ClientConfig*   Config_;
   f9OmsRc_CoreTDay              CoreTDay_;
   f9OmsRc_SNO                   LastSNO_;
   // 為了簡化測試, 最多支援 n 個下單要求.
   RequestRec  RequestRecs_[16];
} UserDefine;
fon9_WARN_POP;
//--------------------------------------------------------------------------//
void OnClientLinkEv(f9rc_ClientSession* ses, f9io_State st, fon9_CStrView info) {
   (void)st; (void)info;
   UserDefine* ud = ses->UserData_;
   ud->Config_ = NULL;
}
void OnClientConfig(f9rc_ClientSession* ses, const f9OmsRc_ClientConfig* cfg) {
   UserDefine* ud = ses->UserData_;
   ud->Config_ = cfg;
   if (f9OmsRc_IsCoreTDayChanged(&ud->CoreTDay_, &cfg->CoreTDay_)) {
      ud->LastSNO_ = 0;
      ud->CoreTDay_ = cfg->CoreTDay_;
   }
   f9OmsRc_SubscribeReport(ses, cfg, ud->LastSNO_ + 1, f9OmsRc_RptFilter_AllPass);
}
void OnClientReport(f9rc_ClientSession* ses, const f9OmsRc_ClientReport* rpt) {
   UserDefine* ud = ses->UserData_;
   if (fon9_LIKELY(rpt->Layout_)) {
      if (rpt->ReportSNO_ == 0) {
         // 尚未進入 OmsCore 之前就被拒絕, OmsCore log 不會記錄此筆訊息.
         return;
      }
   }
   else { // if (rpt->Layout_ == NULL) // 回補結束.
   }
   ud->LastSNO_ = rpt->ReportSNO_;
}
void OnClientFcReq(f9rc_ClientSession* ses, unsigned usWait) {
   // 也可不提供此 function: f9OmsRc_ClientHandler.FnOnFlowControl_ = NULL;
   // 則由 API 判斷超過流量時: 等候解除流量管制 => 送出下單要求 => 返回下單要求呼叫端.
   printf("OnClientFcReq|ses=%p|wait=%u us\n", ses, usWait);
   fon9_SleepMS((usWait + 999) / 1000);
}
//--------------------------------------------------------------------------//
void PrintRequest(const f9OmsRc_Layout* pReqLayout, const RequestRec* req) {
   printf("[%u] %s\n", pReqLayout->LayoutId_, pReqLayout->LayoutName_.Begin_);
   for (unsigned iFld = 0; iFld < pReqLayout->FieldCount_; ++iFld) {
      const f9OmsRc_LayoutField* fld = &pReqLayout->FieldArray_[iFld];
      printf("\t[%2u] %-6s %-10s = '%s'\n", iFld, fld->TypeId_, fld->Named_.Name_.Begin_, req->Fields_[iFld]);
   }
}
void PrintConfig(const UserDefine* ud) {
   if (!ud->Config_) {
      puts("Config not ready.");
      return;
   }
   // TODO: 直接在此輸出並不安全, 要先 lock ud, 然後才能安全的取得並使用 ud->Config_.
   const f9OmsRc_ClientConfig* cfg = ud->Config_;
   printf("HostId = %u\n", cfg->CoreTDay_.HostId_);
   printf("TDay   = %u/%u\n", cfg->CoreTDay_.YYYYMMDD_, cfg->CoreTDay_.UpdatedCount_);
   printf("Tables =\n%s\n", cfg->RightsTables_.OrigStrView_.Begin_);

   for (unsigned iReqLayout = 0; iReqLayout < cfg->RequestLayoutCount_; ++iReqLayout)
      PrintRequest(cfg->RequestLayoutArray_[iReqLayout], &ud->RequestRecs_[iReqLayout]);
}

const f9OmsRc_Layout* GetRequestLayout(UserDefine* ud, char** cmd) {
   if (!ud->Config_) {
      puts("Config not ready.");
      return NULL;
   }
   // Req(Id or Name)
   const char* reqName = (*cmd ? fon9_StrCutSpace(*cmd, cmd) : "");
   if (isdigit(*reqName)) {
      unsigned id = strtoul(reqName, NULL, 10);
      if (id <= 0 || ud->Config_->RequestLayoutCount_ < id) {
         printf("Unknown request id = %s\n", reqName);
         return NULL;
      }
      return ud->Config_->RequestLayoutArray_[id - 1];
   }
   const f9OmsRc_Layout* pReqLayout = f9OmsRc_GetRequestLayout(ud->Session_, ud->Config_, reqName);
   if (pReqLayout)
      return pReqLayout;
   printf("Unknown request name = %s\n", reqName);
   return NULL;
}
void MakeRequestStr(const f9OmsRc_Layout* pReqLayout, RequestRec* req) {
   char* reqstr = req->RequestStr_;
   for (unsigned iFld = 0; iFld < pReqLayout->FieldCount_; ++iFld) {
      const char* str = req->Fields_[iFld];
      size_t      len = strlen(str);
      memcpy(reqstr, str, len);
      *(reqstr += len) = '\x01';
      ++reqstr;
   }
   *reqstr = '\0';
}

int FetchFieldIndex(char** pcmd, const f9OmsRc_Layout* pReqLayout) {
   unsigned iFld;
   if (isdigit((unsigned char)**pcmd)) {
      iFld = strtoul(*pcmd, pcmd, 10);
      if (iFld >= pReqLayout->FieldCount_) {
         printf("Unknwon field index = %u\n", iFld);
         return -1;
      }
   }
   else {
      const char* fldName = fon9_StrFetchNoTrim(*pcmd, (const char**)pcmd, "= \t");
      char ch = **pcmd;
      **pcmd = '\0';
      for (iFld = 0; iFld < pReqLayout->FieldCount_; ++iFld) {
         if (strcmp(pReqLayout->FieldArray_[iFld].Named_.Name_.Begin_, fldName) == 0)
            goto __FOUND_FIELD;
      }
      printf("Field not found: %s\n", fldName);
      return -1;
   __FOUND_FIELD:
      **pcmd = ch;
   }
   *pcmd = fon9_StrTrimHead(*pcmd);
   if (**pcmd != '=') {
      printf("Loss '=' for field.");
      return -1;
   }
   *pcmd = fon9_StrTrimHead(*pcmd + 1);
   return (int)iFld;
}
void SetRequest(UserDefine* ud, char* cmd) {
   // Req(Id or Name) fld(Index or Name)=value|fld2=val2|fld3=val3...
   const f9OmsRc_Layout* pReqLayout = GetRequestLayout(ud, &cmd);
   if (!pReqLayout)
      return;
   RequestRec* req = &ud->RequestRecs_[pReqLayout->LayoutId_ - 1];
   unsigned    iFld;
   if (cmd == NULL)
      goto __BREAK_PUT_FIELDS;
   for (;;) {
      if ((int)(iFld = FetchFieldIndex(&cmd, pReqLayout)) < 0)
         break;
      char* val;
      switch (*cmd) {
      case '\'': case '"':
         cmd = strchr(val = cmd + 1, *cmd);
         if (cmd == NULL) {
            printf("Cannot find matching [%c].\n", *(val - 1));
            goto __BREAK_PUT_FIELDS;
         }
         *cmd = '\0';
         cmd = fon9_StrTrimHead(cmd + 1);
         if (*cmd == '|')
            ++cmd;
         break;
      default:
         val = (char*)fon9_StrFetchNoTrim(cmd, (const char**)&cmd, "|");
         int isEOS = (*cmd == '\0');
         *fon9_StrTrimTail(val, cmd) = '\0';
         if (isEOS)
            cmd = NULL;
         break;
      }
      strncpy(req->Fields_[iFld], val, sizeof(req->Fields_[iFld]) - 1);
      req->Fields_[iFld][sizeof(req->Fields_[iFld]) - 1] = '\0';
      if (!cmd)
         break;
      cmd = fon9_StrTrimHead(cmd + 1);
   }
__BREAK_PUT_FIELDS:
   MakeRequestStr(pReqLayout, req);
   PrintRequest(pReqLayout, req);
   printf("RequestStr = [%s]\n", req->RequestStr_);
}
//--------------------------------------------------------------------------//
typedef struct {
   f9rc_ClientSession*     Session_;
   const f9OmsRc_Layout*   ReqLayout_;
   RequestRec*             ReqRec_;
   unsigned long           IntervalMS_;
   unsigned long           Times_;
} SendArgs;

#define kMaxLoopValueCount 100
typedef struct {
   fon9_CStrView  Values_[kMaxLoopValueCount];
   unsigned       ValueCount_;
   unsigned       FieldIndex_;
} LoopField;

uint64_t SendGroup(char* cmd, const SendArgs args) {
   fon9_CStrView  reqFieldArray[kMaxFieldCount];
   for (unsigned L = 0; L < args.ReqLayout_->FieldCount_; ++L) {
      reqFieldArray[L].Begin_ = args.ReqRec_->Fields_[L];
      reqFieldArray[L].End_ = memchr(args.ReqRec_->Fields_[L], '\0', sizeof(args.ReqRec_->Fields_[L]));
   }
   const char* groupName = fon9_StrCutSpace(cmd, &cmd);
   LoopField   loopFields[kMaxFieldCount];
   unsigned    loopFieldCount = 0;
   fon9_CStrView* pAutoOrdNo = NULL; // OrdNo 自動累加.
   if (cmd) {
      if (*cmd == '|') {
         // "|FieldName1=V1,V2,V3...|FieldName2=v1,v2..."
         // value 使用「,」分隔, 不支援特殊字元及引號.
         for (;;) {
            LoopField* fld = loopFields + loopFieldCount;
            ++cmd;
            fld->FieldIndex_ = (unsigned)FetchFieldIndex(&cmd, args.ReqLayout_);
            if ((int)fld->FieldIndex_ < 0)
               return 0;
            fld->ValueCount_ = 0;
            ++loopFieldCount;
            for (;;) {
               fon9_CStrView* val = &fld->Values_[fld->ValueCount_++];
               val->Begin_ = fon9_StrFetchNoTrim(fon9_StrTrimHead(cmd), &val->End_, ",|");
               char chSpl = *(cmd = (char*)val->End_);
               val->End_ = fon9_StrTrimTail((char*)val->Begin_, cmd);
               *((char*)val->End_) = '\0';
               if (chSpl == '\0')
                  goto __CMD_PARSE_END;
               if (chSpl == '|')
                  break;
               ++cmd;
            }
         }
      }
      else if (*cmd == '+') {
         if (args.ReqLayout_->IdxOrdNo_ >= 0)
            pAutoOrdNo = &reqFieldArray[args.ReqLayout_->IdxOrdNo_];
      }
   }
__CMD_PARSE_END:;
   uint64_t usBeg = fon9_GetSystemUS();
   fon9_CStrView* pClOrdId = (args.ReqLayout_->IdxClOrdId_ >= 0) ? &reqFieldArray[args.ReqLayout_->IdxClOrdId_] : NULL;
   fon9_CStrView* pUsrDef = (args.ReqLayout_->IdxUsrDef_ >= 0) ? &reqFieldArray[args.ReqLayout_->IdxUsrDef_] : NULL;
   for (unsigned long L = 0; L < args.Times_; ++L) {
      if (loopFieldCount) {
         const LoopField* fld = loopFields;
         for (unsigned lc = 0; lc < loopFieldCount; ++lc) {
            reqFieldArray[fld->FieldIndex_] = fld->Values_[L % fld->ValueCount_];
            ++fld;
         }
      }
      if (pAutoOrdNo)
         f9omstw_IncStrAlpha((char*)pAutoOrdNo->Begin_, (char*)pAutoOrdNo->End_);
      if (pClOrdId)
         pClOrdId->End_ = pClOrdId->Begin_ + sprintf((char*)pClOrdId->Begin_, "%s:%lu", groupName, L + 1);
      if (pUsrDef)
         pUsrDef->End_ = pUsrDef->Begin_ + sprintf((char*)pUsrDef->Begin_, "%" PRIu64, fon9_GetSystemUS());
      f9OmsRc_SendRequestFields(args.Session_, args.ReqLayout_, reqFieldArray);
      if (args.IntervalMS_ > 0)
         fon9_SleepMS(args.IntervalMS_);
   }
   if (pClOrdId)
      *(char*)(pClOrdId->Begin_) = '\0';
   if (pUsrDef)
      *(char*)(pUsrDef->Begin_) = '\0';
   return usBeg;
}
void SendRequest(UserDefine* ud, char* cmd) {
   // send Req(Id or Name) times [GroupId]
   SendArgs args;
   args.ReqLayout_ = GetRequestLayout(ud, &cmd);
   if (!args.ReqLayout_)
      return;
   args.ReqRec_ = &ud->RequestRecs_[args.ReqLayout_->LayoutId_ - 1];
   args.Session_ = ud->Session_;

   args.Times_ = (cmd ? strtoul(cmd, &cmd, 10) : 1);
   if (args.Times_ == 0)
      args.Times_ = 1;
   args.IntervalMS_ = 0;
   if (cmd) {
      cmd = fon9_StrTrimHead(cmd);
      if (*cmd == '/') { // times/msInterval
         args.IntervalMS_ = strtoul(cmd + 1, &cmd, 10);
         cmd = fon9_StrTrimHead(cmd);
      }
   }
   fon9_CStrView  reqstr;
   reqstr.Begin_ = args.ReqRec_->RequestStr_;
   reqstr.End_ = memchr(reqstr.Begin_, '\0', sizeof(args.ReqRec_->RequestStr_));
   if (reqstr.End_ - reqstr.Begin_ <= 0) {
      puts("Request message is empty?");
      PrintRequest(args.ReqLayout_, args.ReqRec_);
      return;
   }
   uint64_t usBeg;
   if (cmd == NULL || *cmd == '\0') {
      usBeg = fon9_GetSystemUS();
      for (unsigned long L = 0; L < args.Times_; ++L) {
         f9OmsRc_SendRequestString(args.Session_, args.ReqLayout_, reqstr);
         if (args.IntervalMS_ > 0)
            fon9_SleepMS(args.IntervalMS_);
      }
   }
   else if ((usBeg = SendGroup(cmd, args)) <= 0)
      return;

   uint64_t usEnd = fon9_GetSystemUS();
   printf("Begin: %" PRIu64 ".%06" PRIu64 "\n", usBeg / 1000000, usBeg % 1000000);
   printf("  End: %" PRIu64 ".%06" PRIu64 "\n", usEnd / 1000000, usEnd % 1000000);
   printf("Spent: %" PRIu64 " us / %lu times = %lf\n",
          usEnd - usBeg, args.Times_, (usEnd - usBeg) / (double)args.Times_);
}
//--------------------------------------------------------------------------//
void OnSvConfig(f9rc_ClientSession* ses, const f9sv_ClientConfig* cfg) {
   (void)ses;
   printf("OnSvConfig: FcQry=%u/%u|MaxSubrCount=%u\n" "{%s}\n",
          cfg->FcQryCount_, cfg->FcQryMS_, cfg->MaxSubrCount_,
          cfg->RightsTables_.OrigStrView_.Begin_);
}
void OnSvReport(f9rc_ClientSession* ses, const f9sv_ClientReport* rpt) {
   (void)ses;
   printf("OnSvReport(UserDefine=%u), result=%d\n"
          "treePath={%s}, seedKey={%s}, tab{%s/%u}\n",
         (unsigned)((uintptr_t)rpt->UserData_),
          rpt->ResultCode_,
          rpt->TreePath_.Begin_,
          rpt->SeedKey_.Begin_,
          rpt->Tab_->Named_.Name_.Begin_,
          rpt->Tab_->Named_.Index_);
   if (rpt->Seed_) {
      const f9sv_Field* fld = rpt->Tab_->FieldArray_;
      unsigned fldidx = rpt->Tab_->FieldCount_;
      if (fldidx) {
         for (;;) {
            char  buf[1024];
            printf("%s=[%s]", fld->Named_.Name_.Begin_,
                   f9sv_GetField_StrN(rpt->Seed_, fld, buf, sizeof(buf)));
            if (--fldidx <= 0)
               break;
            printf("|");
            ++fld;
         }
         printf("\n");
      }
   }
}
//--------------------------------------------------------------------------//
const char  kCSTR_LogFileFmt[] =
"   LogFileFmt:\n"
"     '' = ./logs/{0:f+'L'}/f9OmsRc.log\n"
"     time format: {0:?}\n"
"       L = YYYYMMDDHHMMSS\n"
"       f = YYYYMMDD\n"
"       F = YYYY-MM-DD\n"
"       K = YYYY/MM/DD\n"
"       Y = YYYY\n"
"       m = MM = Month\n"
"       d = DD = Day\n"
"       +'L' = to localtime\n";
const char  kCSTR_LogFlags[] =
"   LogFlags(hex):\n"
"     1 = f9rc_ClientLogFlag_Link\n"
"     100 = f9rc_ClientLogFlag_Request & Config\n"
"     200 = f9rc_ClientLogFlag_Report  & Config\n"
"     400 = f9rc_ClientLogFlag_Config\n";
//--------------------------------------------------------------------------//
int main(int argc, char* argv[]) {
#if defined(_MSC_VER) && defined(_DEBUG)
   _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
   //_CrtSetBreakAlloc(176);
   SetConsoleCP(CP_UTF8);
   SetConsoleOutputCP(CP_UTF8);
#endif
   const char* logFileFmt = NULL;

   f9rc_ClientSessionParams  f9rcCliParams;
   memset(&f9rcCliParams, 0, sizeof(f9rcCliParams));
   f9rcCliParams.DevName_ = "TcpClient";
   f9rcCliParams.LogFlags_ = f9rc_ClientLogFlag_All;

   f9OmsRc_ClientSessionParams   omsRcParams;
   f9OmsRc_InitClientSessionParams(&f9rcCliParams, &omsRcParams);

   f9rcCliParams.FnOnLinkEv_ = &OnClientLinkEv;
   omsRcParams.FnOnConfig_ = &OnClientConfig;
   omsRcParams.FnOnReport_ = &OnClientReport;
   omsRcParams.FnOnFlowControl_ = &OnClientFcReq;

   const char** pargv = (const char**)argv;
   for (int L = 1; L < argc;) {
      const char* parg = *++pargv;
      switch (parg[0]) {
      case '-': case '/':  break;
      default:
   __UNKNOWN_ARGUMENT:
         printf("Unknown argument: %s\n", parg);
         goto __USAGE;
      }
      L += 2;
      ++pargv;
      switch (parg[1]) {
      case 'l':   logFileFmt = *pargv;               break;
      case 'f':   f9rcCliParams.LogFlags_ = (f9rc_ClientLogFlag)strtoul(*pargv, NULL, 16);    break;
      case 'u':   f9rcCliParams.UserId_ = *pargv;    break;
      case 'p':   f9rcCliParams.Password_ = *pargv;  break;
      case 'n':   f9rcCliParams.DevName_ = *pargv;   break;
      case 'a':   f9rcCliParams.DevParams_ = *pargv; break;
      case 't':
         f9omstw_FreeOmsErrMsgTx(omsRcParams.ErrCodeTx_);
         omsRcParams.ErrCodeTx_ = f9omstw_LoadOmsErrMsgTx1(*pargv);
         break;
      case '?':   goto __USAGE;
      default:    goto __UNKNOWN_ARGUMENT;
      }
   }
   if(f9rcCliParams.UserId_ == NULL
   || f9rcCliParams.DevName_ == NULL
   || f9rcCliParams.DevParams_ == NULL) {
__USAGE:
      printf("Usage:\n"
             "-l LogFileFmt\n"
             "   default is log to console\n"
             "%s"
             "-f LogFlags(hex)\n"
             "   default is ffff = all of below.\n"
             "%s"
             "-n DevName\n"
             "   default is TcpClient\n"
             "-a DevArguments\n"
             "   e.g. -a 127.0.0.1:6601\n"
             "   e.g. -a dn=localhost:6601\n"
             "-u UserId\n"
             "-p Password\n"
             "-t OmsErrCode.All.cfg:zh\n",
             kCSTR_LogFileFmt, kCSTR_LogFlags);
      return 3;
   }
   char  passwd[1024];
   if (f9rcCliParams.Password_ == NULL) {
      fon9_getpass(stdout, "Password: ", passwd, sizeof(passwd));
      f9rcCliParams.Password_ = passwd;
   }
   // ----------------------------
   f9OmsRc_Initialize(logFileFmt);

   f9sv_ClientSessionParams   svRcParams;
   f9sv_InitClientSessionParams(&f9rcCliParams, &svRcParams);
   svRcParams.FnOnConfig_ = &OnSvConfig;
   f9sv_Initialize(NULL);
   fon9_Finalize();

   UserDefine  ud;
   memset(&ud, 0, sizeof(ud));
   f9rcCliParams.UserData_ = &ud;
   f9rc_CreateClientSession(&ud.Session_, &f9rcCliParams);
   // ----------------------------
   char  cmdbuf[4096];
   for (;;) {
      printf("> ");
      if (!fgets(cmdbuf, sizeof(cmdbuf), stdin))
         break;
      char* pend = fon9_StrTrimTail(cmdbuf, memchr(cmdbuf, '\0', sizeof(cmdbuf)));
      if (pend == cmdbuf)
         continue;
      *pend = '\0';
      char* pbeg = fon9_StrCutSpace(cmdbuf, &pend);
      if (strcmp(pbeg, "quit") == 0)
         goto __QUIT;
      else if (strcmp(pbeg, "cfg") == 0)
         PrintConfig(&ud);
      else if (strcmp(pbeg, "set") == 0)
         SetRequest(&ud, pend);
      else if (strcmp(pbeg, "send") == 0)
         SendRequest(&ud, pend);
      else if (strcmp(pbeg, "lf") == 0) {
         if (pend) {
            ud.Session_->LogFlags_ = (f9rc_ClientLogFlag)strtoul(pend, &pend, 16);
            pend = fon9_StrTrimHead(pend);
         }
         printf("LogFlags = %x\n", ud.Session_->LogFlags_);
         if (pend && *pend) {
            if ((pend[0] == '\'' && pend[1] == '\'')
                || (pend[0] == '"' && pend[1] == '"'))
               pend = "";
            fon9_Initialize(pend);
            fon9_Finalize();
         }
      }
      else if (strcmp(pbeg, "q") == 0) { // query: treePath key tabName
         if (pend == NULL) {
            puts("q: require 'treePath'");
            continue;
         }
         f9sv_SeedName seedName;
         memset(&seedName, 0, sizeof(seedName));
         seedName.TreePath_ = fon9_StrCutSpace(pend, &pend);
         if (pend == NULL) {
            puts("q: require 'key'");
            continue;
         }
         seedName.SeedKey_ = fon9_StrCutSpace(pend, &pend);
         if (pend) {
            if (isdigit((unsigned char)*pend))
               seedName.TabIndex_ = (f9sv_TabSize)strtoul(pend, NULL, 10);
            else
               seedName.TabName_ = pend;
         }
         static f9sv_ReportHandler handler = {&OnSvReport, NULL};
         handler.UserData_ = ((char*)handler.UserData_) + 1;
         f9sv_Result res = f9sv_Query(ud.Session_, &seedName, handler);
         printf("Query(Id=%u): [%s][%s][%s/%d], result=%d\n",
                (unsigned)((uintptr_t)handler.UserData_), seedName.TreePath_, seedName.SeedKey_,
                (seedName.TabName_ ? seedName.TabName_ : "<nil>"),
                seedName.TabIndex_,
                res);
      }
      else if (strcmp(pbeg, "?") == 0 || strcmp(pbeg, "help") == 0)
         printf("quit\n"
                "   Quit program.\n"
                "\n"
                "cfg\n"
                "   List configs.\n"
                "\n"
                "set ReqId(or ReqName) FieldId(or FieldName)=value|fld2=val2|fld3=val3\n"
                "\n"
                "send ReqId(or ReqName) times[/msInterval] [GroupId]\n"
                "\n"
                "lf LogFlags(hex) [LogFileFmt]\n"
                "%s"
                "%s"
                "\n"
                "q treePath key tabName\n"
                "\n"
                "? or help\n"
                "   This info.\n",
                kCSTR_LogFlags, kCSTR_LogFileFmt);
      else
         printf("Unknown command: %s\n", pbeg);
   }
   // ----------------------------
__QUIT:
   f9rc_DestroyClientSession_Wait(ud.Session_);
   fon9_Finalize();
   f9omstw_FreeOmsErrMsgTx(omsRcParams.ErrCodeTx_);
   puts("OmsRcClient test quit.");
}
