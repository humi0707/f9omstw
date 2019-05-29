﻿// \file f9omstw/OmsRequest.hpp
// \author fonwinz@gmail.com
#ifndef __f9omstw_OmsRequest_hpp__
#define __f9omstw_OmsRequest_hpp__
#include "f9omstw/OmsRxItem.hpp"
#include "f9omstw/OmsRequestId.hpp"
#include "f9omstw/OmsRequestPolicy.hpp"
#include "f9omstw/IvacNo.hpp"
#include "fon9/fmkt/Trading.hpp"
#include "fon9/seed/Tab.hpp"

namespace f9omstw {

/// fon9::fmkt::TradingRequest::RequestFlags_;
enum OmsRequestFlag : uint8_t {
   OmsRequestFlag_Initiator = 0x01,
   /// 經由外部回報所建立的 request.
   OmsRequestFlag_External = 0x02,
   /// 無法進入委託流程: 無法建立 OmsOrder, 或找不到對應的 OmsOrder.
   OmsRequestFlag_Abandon = 0x04,
};
fon9_ENABLE_ENUM_BITWISE_OP(OmsRequestFlag);

/// 新、刪、改、查、成交; 都視為一種 request 共同的基底就是 OmsRequestBase;
class OmsRequestBase : public fon9::fmkt::TradingRequest, public OmsRxItem, public OmsRequestId {
   fon9_NON_COPY_NON_MOVE(OmsRequestBase);
   using base = fon9::fmkt::TradingRequest;

   friend class OmsBackend; // 取得修改 LastUpdated_ 的權限.
   union {
      OmsOrderRaw mutable* LastUpdated_{nullptr};
      /// 當 IsEnumContains(this->RequestFlags(), OmsRequestFlag_Abandon) 則沒有 LastUpdated_;
      /// 此時 AbandonReason_ 說明中斷要求的原因.
      std::string*   AbandonReason_;
   };
   fon9::TimeStamp   CrTime_;

   void OnRxItem_AddRef() const override;
   void OnRxItem_Release() const override;
   inline friend void intrusive_ptr_add_ref(const OmsRequestBase* p) {
      intrusive_ptr_add_ref(static_cast<const fon9::fmkt::TradingRequest*>(p));
   }
   inline friend void intrusive_ptr_release(const OmsRequestBase* p) {
      intrusive_ptr_release(static_cast<const fon9::fmkt::TradingRequest*>(p));
   }

   static void MakeFieldsImpl(fon9::seed::Fields& flds);
protected:
   template <class Derived>
   static void MakeFields(fon9::seed::Fields& flds) {
      static_assert(fon9_OffsetOf(Derived, RequestKind_) == fon9_OffsetOf(OmsRequestBase, RequestKind_),
                    "'OmsRequestBase' must be the first base class in derived.");
      MakeFieldsImpl(flds);
   }

public:
   OmsRequestFactory* const   Creator_;
   OmsRequestBase(OmsRequestFactory& creator, f9fmkt_RequestKind reqKind = f9fmkt_RequestKind_Unknown)
      : base(reqKind)
      , CrTime_{fon9::UtcNow()}
      , Creator_(&creator) {
   }
   OmsRequestBase(f9fmkt_RequestKind reqKind = f9fmkt_RequestKind_Unknown)
      : base(reqKind)
      , Creator_(nullptr) {
   }
   void Initialize(OmsRequestFactory& creator, fon9::TimeStamp now = fon9::UtcNow()) {
      *const_cast<OmsRequestFactory**>(&this->Creator_) = &creator;
      this->CrTime_ = now;
   }
   /// 解構時:
   /// if(this->RequestFlags_ & OmsRequestFlag_Abandon) 則刪除 AbandonReason_.
   /// if(this->RequestFlags_ & OmsRequestFlag_Initiator) 則刪除 Order.
   ~OmsRequestBase();

   const OmsRequestBase* CastToRequest() const override;

   void RevPrint(fon9::RevBuffer& rbuf) const override;

   OmsRequestFlag RequestFlags() const {
      return static_cast<OmsRequestFlag>(this->RequestFlags_);
   }
   bool IsInitiator() const {
      return (this->RequestFlags_ & OmsRequestFlag_Initiator) == OmsRequestFlag_Initiator;
   }
   const OmsOrderRaw* LastUpdated() const {
      return IsEnumContains(this->RequestFlags(), OmsRequestFlag_Abandon) ? nullptr : this->LastUpdated_;
   }
   /// 如果傳回非nullptr, 則表示此筆下單要求失敗, 沒有進入系統.
   /// - Abandon 原因可能為: 欄位錯誤(例如: 買賣別無效), 不是可用帳號...
   /// - 進入系統後的失敗會用 Reject 機制, 透過 OmsOrderRaw 處理.
   const std::string* AbandonReason() const {
      return IsEnumContains(this->RequestFlags(), OmsRequestFlag_Abandon) ? this->AbandonReason_ : nullptr;
   }
   void Abandon(std::string reason) {
      assert(this->AbandonReason_ == nullptr);
      assert((this->RequestFlags_ & (OmsRequestFlag_Abandon | OmsRequestFlag_Initiator)) == OmsRequestFlag{});
      this->AbandonReason_ = new std::string(std::move(reason));
      this->RequestFlags_ |= OmsRequestFlag_Abandon;
   }
};

struct OmsRequestFrom {
   /// 收單處理程序名稱, 例如: Rc, FixIn...
   fon9::CharAry<8>  SesName_;
   fon9::CharAry<12> UserId_;
   fon9::CharAry<16> FromIp_;
   /// 來源別.
   fon9::CharAry<4>  Src_;

   OmsRequestFrom() {
      memset(this, 0, sizeof(*this));
   }
};

struct OmsRequestCliDef {
   fon9::CharAry<16> UsrDef_;
   fon9::CharAry<16> ClOrdId_;

   OmsRequestCliDef() {
      memset(this, 0, sizeof(*this));
   }
};

/// 下單要求(排除成交): 新、刪、改、查; 共同的基底 OmsTradingRequest;
class OmsTradingRequest : public OmsRequestBase,
                          public OmsRequestFrom,
                          public OmsRequestCliDef {
   fon9_NON_COPY_NON_MOVE(OmsTradingRequest);
   using base = OmsRequestBase;

   OmsRequestPolicySP   Policy_;

   static void MakeFieldsImpl(fon9::seed::Fields& flds);
protected:
   template <class Derived>
   static void MakeFields(fon9::seed::Fields& flds) {
      static_assert(fon9_OffsetOf(Derived, SesName_) == fon9_OffsetOf(OmsTradingRequest, SesName_),
                    "'OmsTradingRequest' must be the first base class in derived.");
      MakeFieldsImpl(flds);
   }

public:
   OmsTradingRequest(OmsRequestFactory& creator, f9fmkt_RequestKind reqKind = f9fmkt_RequestKind_Unknown)
      : base{creator, reqKind} {
   }
   OmsTradingRequest(f9fmkt_RequestKind reqKind = f9fmkt_RequestKind_Unknown)
      : base{reqKind} {
   }

   void SetPolicy(OmsRequestPolicySP policy) {
      assert(this->Policy_.get() == nullptr);
      this->Policy_ = std::move(policy);
   }
   const OmsRequestPolicy* Policy() const {
      return this->Policy_.get();
   }

   /// 請參閱「下單流程」文件.
   /// 在建立好 req 之後, 預先檢查程序, 此時還在 user thread, 尚未進入 OmsCore.
   /// return this->Policy_ ? this->Policy_->PreCheckInUser(reqRunner) : true;
   virtual bool PreCheckInUser(OmsRequestRunner& reqRunner);
};

struct OmsRequestNewDat {
   // BrkId: Tws=CharAry<4>; Twf=CharAry<7>;
   // BrkId 放在 OmsRequestNewTws; OmsRequestNewTwf; 裡面.

   IvacNo            IvacNo_;
   fon9::CharAry<10> SubacNo_;
   fon9::CharAry<5>  SalesNo_;
   OmsOrdNo          OrdNo_;

   OmsRequestNewDat() {
      memset(this, 0, sizeof(*this));
   }
};

class OmsRequestNew : public OmsTradingRequest, public OmsRequestNewDat {
   fon9_NON_COPY_NON_MOVE(OmsRequestNew);
   using base = OmsTradingRequest;

   friend class OmsOrder; // OmsOrder 建構時需要呼叫 SetInitiatorFlag() 的權限.
   void SetInitiatorFlag() {
      this->RequestFlags_ |= OmsRequestFlag_Initiator;
   }
   static void MakeFieldsImpl(fon9::seed::Fields& flds);
protected:
   template <class Derived>
   static void MakeFields(fon9::seed::Fields& flds) {
      static_assert(fon9_OffsetOf(Derived, IvacNo_) == fon9_OffsetOf(OmsRequestNew, IvacNo_),
                    "'OmsTradingRequest' must be the first base class in derived.");
      MakeFieldsImpl(flds);
   }

public:
   OmsRequestNew(OmsRequestFactory& creator, f9fmkt_RequestKind reqKind = f9fmkt_RequestKind_New)
      : base{creator, reqKind} {
   }
   OmsRequestNew(f9fmkt_RequestKind reqKind = f9fmkt_RequestKind_New)
      : base{reqKind} {
   }

   /// 檢查: 若 OrdNo_.begin() != '\0'; 則必須有 IsAllowAnyOrdNo_ 權限.
   bool PreCheckInUser(OmsRequestRunner& reqRunner) override;

   virtual OmsBrk* GetBrk(OmsResource& res) const = 0;
};

class OmsRequestUpd : public OmsTradingRequest {
   fon9_NON_COPY_NON_MOVE(OmsRequestUpd);
   using base = OmsTradingRequest;

   OmsRxSNO IniSNO_{0};

   static void MakeFieldsImpl(fon9::seed::Fields& flds);
protected:
   template <class Derived>
   static void MakeFields(fon9::seed::Fields& flds) {
      static_assert(fon9_OffsetOf(Derived, IniSNO_) == fon9_OffsetOf(OmsRequestUpd, IniSNO_),
                    "'OmsRequestUpd' must be the first base class in derived.");
      MakeFieldsImpl(flds);
   }

public:
   using base::base;
   OmsRequestUpd() = default;

   OmsRxSNO IniSNO() const {
      return this->IniSNO_;
   }
   // 衍生者應覆寫 PreCheckInUser() 檢查並設定 RequestKind, 然後透過 base::PreCheckInUser() 繼續檢查.
   // bool PreCheckInUser(OmsRequestRunner& reqRunner) override;
};

/// 成交回報.
/// 在 OmsOrder 提供:
/// \code
///    OmsRequestMatch* OmsOrder::MatchHead_;
///    OmsRequestMatch* OmsOrder::MatchLast_;
/// \endcode
/// 新的成交, 如果是在 MatchLast_->MatchKey_ 之後, 就直接 append; 否則就從頭搜尋.
/// 由於成交回報「大部分」是附加到尾端, 所以這樣的處理負擔應是最小.
class OmsRequestMatch : public OmsRequestBase {
   fon9_NON_COPY_NON_MOVE(OmsRequestMatch);
   using base = OmsRequestBase;

   const OmsRequestMatch mutable* Next_{nullptr};

   /// 成交回報要求與下單線路無關, 所以這裡 do nothing.
   void NoReadyLineReject(fon9::StrView) override;

   /// 將 curr 插入 this 與 this->Next_ 之間;
   void InsertAfter(const OmsRequestMatch* curr) const {
      assert(curr->Next_ == nullptr && this->MatchKey_ < curr->MatchKey_);
      assert(this->Next_ == nullptr || curr->MatchKey_ < this->Next_->MatchKey_);
      curr->Next_ = this->Next_;
      this->Next_ = curr;
   }

   OmsRxSNO IniSNO_;
   uint64_t MatchKey_{0};

   static void MakeFieldsImpl(fon9::seed::Fields& flds);
protected:
   template <class Derived>
   static void MakeFields(fon9::seed::Fields& flds) {
      static_assert(fon9_OffsetOf(Derived, IniSNO_) == fon9_OffsetOf(OmsRequestMatch, IniSNO_),
                    "'OmsRequestMatch' must be the first base class in derived.");
      MakeFieldsImpl(flds);
   }

public:
   using MatchKey = uint64_t;

   OmsRequestMatch(OmsRequestFactory& creator)
      : base{creator, f9fmkt_RequestKind_Match} {
   }
   OmsRequestMatch()
      : base{f9fmkt_RequestKind_Match} {
   }

   /// 將 curr 依照 MatchKey_ 的順序(小到大), 加入到「成交串列」.
   /// \retval nullptr  成功將 curr 加入成交串列.
   /// \retval !nullptr 與 curr->MatchKey_ 相同的那個 request.
   static const OmsRequestMatch* Insert(const OmsRequestMatch** ppHead, const OmsRequestMatch** ppLast, const OmsRequestMatch* curr);

   const OmsRequestMatch* Next() {
      return this->Next_;
   }
};

} // namespaces
#endif//__f9omstw_OmsRequest_hpp__
