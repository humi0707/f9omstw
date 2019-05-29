﻿// \file f9omstw/OmsRequest.cpp
// \author fonwinz@gmail.com
#include "f9omstw/OmsOrder.hpp"
#include "f9omstw/OmsRequestRunner.hpp"
#include "fon9/seed/FieldMaker.hpp"

namespace f9omstw {

OmsRequestBase::~OmsRequestBase() {
   if (this->RequestFlags_ & OmsRequestFlag_Abandon)
      delete this->AbandonReason_;
   else if (this->RequestFlags_ & OmsRequestFlag_Initiator) {
      assert(this->LastUpdated() != nullptr);
      this->LastUpdated()->Order_->FreeThis();
   }
}
const OmsRequestBase* OmsRequestBase::CastToRequest() const {
   return this;
}
void OmsRequestBase::OnRxItem_AddRef() const {
   intrusive_ptr_add_ref(this);
}
void OmsRequestBase::OnRxItem_Release() const {
   intrusive_ptr_release(this);
}

void OmsRequestBase::MakeFieldsImpl(fon9::seed::Fields& flds) {
   flds.Add(fon9_MakeField(fon9::Named{"Kind"},      OmsRequestBase, RequestKind_));
   flds.Add(fon9_MakeField(fon9::Named{"Market"},    OmsRequestBase, Market_));
   flds.Add(fon9_MakeField(fon9::Named{"SessionId"}, OmsRequestBase, SessionId_));
   flds.Add(fon9_MakeField(fon9::Named{"ReqUID"},    OmsRequestBase, ReqUID_));
   flds.Add(fon9_MakeField(fon9::Named{"CrTime"},    OmsRequestBase, CrTime_));
}

void OmsTradingRequest::MakeFieldsImpl(fon9::seed::Fields& flds) {
   base::MakeFields<OmsTradingRequest>(flds);
   flds.Add(fon9_MakeField(fon9::Named{"SesName"}, OmsTradingRequest, SesName_));
   flds.Add(fon9_MakeField(fon9::Named{"UserId"},  OmsTradingRequest, UserId_));
   flds.Add(fon9_MakeField(fon9::Named{"FromIp"},  OmsTradingRequest, FromIp_));
   flds.Add(fon9_MakeField(fon9::Named{"Src"},     OmsTradingRequest, Src_));
   flds.Add(fon9_MakeField(fon9::Named{"UsrDef"},  OmsTradingRequest, UsrDef_));
   flds.Add(fon9_MakeField(fon9::Named{"ClOrdId"}, OmsTradingRequest, ClOrdId_));
}
bool OmsTradingRequest::PreCheckInUser(OmsRequestRunner& reqRunner) {
   return this->Policy_ ? this->Policy_->PreCheckInUser(reqRunner) : true;
}
//--------------------------------------------------------------------------//
bool OmsRequestNew::PreCheckInUser(OmsRequestRunner& reqRunner) {
   if (*this->OrdNo_.begin() != '\0') {
      const OmsRequestPolicy* pol = this->Policy();
      if (pol == nullptr || !pol->IsAllowAnyOrdNo()) {
         reqRunner.RequestAbandon(nullptr, "OrdNo must empty.");
         return false;
      }
   }
   return base::PreCheckInUser(reqRunner);
}
void OmsRequestNew::MakeFieldsImpl(fon9::seed::Fields& flds) {
   base::MakeFields<OmsRequestNew>(flds);
   flds.Add(fon9_MakeField(fon9::Named{"IvacNo"},  OmsRequestNew, IvacNo_));
   flds.Add(fon9_MakeField(fon9::Named{"SubacNo"}, OmsRequestNew, SubacNo_));
   flds.Add(fon9_MakeField(fon9::Named{"SalesNo"}, OmsRequestNew, SalesNo_));
   flds.Add(fon9_MakeField(fon9::Named{"OrdNo"},   OmsRequestNew, OrdNo_));
}
//--------------------------------------------------------------------------//
void OmsRequestUpd::MakeFieldsImpl(fon9::seed::Fields& flds) {
   flds.Add(fon9_MakeField(fon9::Named{"IniSNO"}, OmsRequestUpd, IniSNO_));
   base::MakeFields<OmsRequestUpd>(flds);
}
//--------------------------------------------------------------------------//
void OmsRequestMatch::MakeFieldsImpl(fon9::seed::Fields& flds) {
   flds.Add(fon9_MakeField(fon9::Named{"IniSNO"},   OmsRequestMatch, IniSNO_));
   flds.Add(fon9_MakeField(fon9::Named{"MatchKey"}, OmsRequestMatch, MatchKey_));
   base::MakeFields<OmsRequestMatch>(flds);
}
void OmsRequestMatch::NoReadyLineReject(fon9::StrView) {
}
const OmsRequestMatch* OmsRequestMatch::Insert(const OmsRequestMatch** ppHead,
                                               const OmsRequestMatch** ppLast,
                                               const OmsRequestMatch* curr) {
   if (const OmsRequestMatch* chk = *ppLast) {
      assert(*ppHead != nullptr);
      if (fon9_LIKELY(chk->MatchKey_ < curr->MatchKey_)) {
         *ppLast = curr;
         chk->InsertAfter(curr);
      }
      else if (curr->MatchKey_ < (chk = *ppHead)->MatchKey_) {
         *ppHead = curr;
         curr->InsertAfter(chk);
      }
      else {
         for (;;) {
            if (chk->MatchKey_ == curr->MatchKey_)
               return chk;
            assert(chk->Next_ != nullptr);
            if (chk->MatchKey_ < curr->MatchKey_ && curr->MatchKey_ < chk->Next_->MatchKey_) {
               chk->InsertAfter(curr);
               break;
            }
            chk = chk->Next_;
         }
      }
   }
   else {
      assert(*ppHead == nullptr);
      *ppHead = *ppLast = curr;
   }
   return nullptr;
}

} // namespaces
