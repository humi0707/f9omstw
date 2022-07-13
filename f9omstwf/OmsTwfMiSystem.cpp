﻿// \file f9omsstgy/OmsTwfMiSystem.cpp
//
// Oms Market data from [TwfMi]
//
// \author fonwinz@gmail.com
#include "f9omstw/OmsCoreMgr.hpp"
#include "f9omstwf/OmsTwfExgMapMgr.hpp"
#include "fon9/seed/Plugins.hpp"
#include "f9twf/ExgMiSystemBase.hpp"
#include "f9twf/ExgMdFmtMatch.hpp"
#include "f9twf/ExgMdFmtI060.hpp"
#include "f9twf/ExgMdFmtSysInfo.hpp" // I140:306 收盤訊息, 通知 Symb.

namespace f9omstw {

class OmsTwfMiSystem : public f9twf::ExgMiSystemBase {
   fon9_NON_COPY_NON_MOVE(OmsTwfMiSystem);
   using base = f9twf::ExgMiSystemBase;
public:
   const OmsCoreMgrSP   OmsCoreMgr_;

   template <class... ArgsT>
   OmsTwfMiSystem(OmsCoreMgrSP omsCoreMgr, ArgsT&&... args)
      : base(std::forward<ArgsT>(args)...)
      , OmsCoreMgr_{std::move(omsCoreMgr)} {
   }
   ~OmsTwfMiSystem() {
   }
   void OnMiChannelNoData(f9twf::ExgMiChannel& ch) override {
      auto core = this->OmsCoreMgr_->CurrentCore();
      if (!core)
         return;
      core->RunCoreTask([this, &ch](OmsResource& coreResource) {
         auto mkt = ch.Market_;
         auto sid = this->TradingSessionId_;
         auto symbs = coreResource.Symbs_->SymbMap_.Lock();
         for (const auto& isymb : *symbs) {
            if (auto* symb = static_cast<OmsSymb*>(isymb.second.get())) {
               if (symb->TradingMarket_ == mkt && symb->TradingSessionId_ == sid) {
                  symb->OnMdNoData(coreResource);
               }
            }
         }
      });
   }

   static bool StartupPlugins(fon9::seed::PluginsHolder& holder, fon9::StrView args);
};

struct OmsTwfMiI020 : public f9twf::ExgMiHandlerPkCont {
   fon9_NON_COPY_NON_MOVE(OmsTwfMiI020);
   using base = f9twf::ExgMiHandlerPkCont;
   using base::base;
   void PkContOnReceived(const void* pk, unsigned pksz, SeqT seq) override {
      (void)pksz;
      this->CheckLogLost(pk, seq);
      auto& coreMgr = *static_cast<OmsTwfMiSystem*>(&this->PkSys_.MiSystem_)->OmsCoreMgr_;
      auto  core = coreMgr.CurrentCore();
      if (!core)
         return;
      auto& pkI020 = *static_cast<const f9twf::ExgMiI020*>(pk);
      auto  symb = core->GetSymbs()->FetchOmsSymb(fon9::StrView_eos_or_all(pkI020.ProdId_.Chars_, ' '));
      if (!symb)
         return;
      auto* twfsymb = dynamic_cast<TwfExgSymbBasic*>(symb.get());
      if (!twfsymb)
         return;
      if (fon9_UNLIKELY(symb->PriceOrigDiv_ == 0 && pkI020.ProdId_.Chars_[5] == '/')) {
         // 期貨價差: 從leg1取得相關必要欄位. TODO:應在建立新的 symb 時填入 PriceOrigDiv_ 及相關事項(例: Contract_).
         if (auto leg1 = core->GetSymbs()->GetOmsSymb(fon9::StrView{pkI020.ProdId_.Chars_, 5})) {
            symb->PriceOrigDiv_ = leg1->PriceOrigDiv_;
         }
      }
      const uint8_t                       count = static_cast<uint8_t>(pkI020.MatchDisplayItem_ & 0x7f);
      const f9twf::ExgMdMatchData* const  pend = pkI020.MatchData_ + count;
      if (!twfsymb->IsNeedsOnMdLastPriceEv()) {
         // 不需要發行事件通知, 則只需要取出 [最後一個] 價格即可。
         (count ? (pend - 1)->MatchPrice_ : pkI020.FirstMatchPrice_)
            .AssignTo(twfsymb->LastPrice_, symb->PriceOrigDiv_);
         auto* pcnt = reinterpret_cast<const f9twf::ExgMdMatchCnt*>(pend);
         twfsymb->TotalQty_ = fon9::PackBcdTo<fon9::fmkt::Qty>(pcnt->MatchTotalQty_);
      }
      else {
         const f9twf::ExgMdMatchData*  pbeg = pkI020.MatchData_;
         OmsMdLastPrice bf = *twfsymb;
         pkI020.FirstMatchPrice_.AssignTo(twfsymb->LastPrice_, symb->PriceOrigDiv_);
         twfsymb->TotalQty_ += fon9::PackBcdTo<fon9::fmkt::Qty>(pkI020.FirstMatchQty_);
         for (;;) {
            if (*twfsymb != bf)
               twfsymb->OnMdLastPriceEv(bf, coreMgr);
            if (pbeg == pend)
               break;
            bf = *twfsymb;
            pbeg->MatchPrice_.AssignTo(twfsymb->LastPrice_, symb->PriceOrigDiv_);
            twfsymb->TotalQty_ += fon9::PackBcdTo<fon9::fmkt::Qty>(pbeg->MatchQty_);
            ++pbeg;
         }
      }
   }
};
struct OmsTwfMiI060 : public f9twf::ExgMiHandlerPkCont {
   fon9_NON_COPY_NON_MOVE(OmsTwfMiI060);
   using base = f9twf::ExgMiHandlerPkCont;
   using base::base;
   void PkContOnReceived(const void* pk, unsigned pksz, SeqT seq) override {
      (void)pksz;
      this->CheckLogLost(pk, seq);
      auto& pkI060 = *static_cast<const f9twf::ExgMiI060*>(pk);
      if (!IsEnumContains(pkI060.StatusItem1_, f9twf::I060StatusItem1::PriMatch))
         return;
      const char* body = pkI060.Body_;
      OmsTwfPri   lastPrice = OmsTwfPri::Make<5>(fon9::PackBcdTo<uint64_t>(*reinterpret_cast<const f9twf::ExgMiI060::PriceV5*>(body)));
      if (TwfExgMapMgr* exgMap = static_cast<OmsTwfMiSystem*>(&this->PkSys_.MiSystem_)->OmsCoreMgr_->GetTwfExgMapMgr()) {
         if (auto* ctree = exgMap->GetContractTree()) {
            OmsTwfContractId cid{fon9::StrView_eos_or_all(pkI060.Kind_.Chars_, ' ')};
            auto contracts = ctree->ContractMap_.Lock();
            auto icontract = contracts->find(cid);
            if (icontract != contracts->end()) {
               icontract->second->LastPrice_ = lastPrice;
               // contracts.unlock();
               // contract->LastPriceSubject_.Publish();
            }
         }
      }
   }
};
struct OmsTwfMiI140 : public f9twf::ExgMiHandlerAnySeq {
   fon9_NON_COPY_NON_MOVE(OmsTwfMiI140);
   using base = f9twf::ExgMiHandlerAnySeq;
   using base::base;
   void OnPkReceived(const f9twf::ExgMiHead& pk, unsigned pksz) override {
      (void)pksz;
      auto& pkI140 = *static_cast<const f9twf::ExgMiI140*>(&pk);
      auto& i30x = f9twf::I140CastTo<const f9twf::ExgMdSysInfo30x>(pkI140);
      if (fon9::PackBcdTo<unsigned>(pkI140.FunctionCode_) != 306)
         return;
      if (i30x.ListType_ != f9twf::ExgMdSysInfo_ListType::FlowGroup)
         return;
      auto& coreMgr = *static_cast<OmsTwfMiSystem*>(&this->PkSys_.MiSystem_)->OmsCoreMgr_;
      auto  core = coreMgr.CurrentCore();
      if (!core)
         return;
      auto flowGroup = fon9::PackBcdTo<uint8_t>(i30x.List_.FlowGroup_);
      core->RunCoreTask([this, flowGroup](OmsResource& coreResource) {
         auto mkt = this->PkSys_.Market_;
         auto sid = this->PkSys_.MiSystem_.TradingSessionId_;
         auto symbs = coreResource.Symbs_->SymbMap_.Lock();
         for (const auto& isymb : *symbs) {
            if (auto* symb = static_cast<OmsSymb*>(isymb.second.get())) {
               if (symb->TradingMarket_ == mkt
                   && symb->TradingSessionId_ == sid
                   && symb->FlowGroup_ == flowGroup) {
                  symb->TradingSessionSt_ = f9fmkt_TradingSessionSt_Closed;
                  symb->OnTradingSessionClosed(coreResource);
               }
            }
         }
      });
   }
};

// OmsCore=omstw|Io="..."|Add=TradingSessionId,DataInSch
// TradingSessionId: N=f9fmkt_TradingSessionId_Normal, A=f9fmkt_TradingSessionId_AfterHour;
// 例: OmsCore=omstw|Io=/MaIo|Add=N,064500-180000|Add=A,144000-060000
//     Io="ThreadCount=2|Capacity=10|Cpus=1"
//     Io 設定必須在 Add 之前; 若 Add [之間] 沒有設定 Io, 則[後面]的 Add 使用[前面]的 IoServiceSrc_;
bool OmsTwfMiSystem::StartupPlugins(fon9::seed::PluginsHolder& holder, fon9::StrView args) {
   OmsCoreMgrSP         omsCoreMgr;
   fon9::StrView        tag, value;
   OmsTwfMiSystem*      pthis{};
   fon9::IoManagerArgs  iomArgs;
   while (SbrFetchTagValue(args, tag, value)) {
      if (fon9::iequals(tag, "OmsCore")) {
         if ((omsCoreMgr = holder.Root_->GetSapling<OmsCoreMgr>(value)).get() == nullptr) {
            holder.SetPluginsSt(fon9::LogLevel::Error, "Not found OmsCore: ", value);
            return false;
         }
      }
      else if (fon9::iequals(tag, "Io")) {
         iomArgs.SetIoServiceCfg(holder, value);
      }
      else if (fon9::iequals(tag, "Add")) {
         if (!omsCoreMgr) {
            holder.SetPluginsSt(fon9::LogLevel::Error, "Unknwon OmsCore");
            return false;
         }
         f9fmkt_TradingSessionId txSessionId{static_cast<f9fmkt_TradingSessionId>(value.Get1st())};
         if ((txSessionId != f9fmkt_TradingSessionId_Normal) && (txSessionId != f9fmkt_TradingSessionId_AfterHour)) {
            holder.SetPluginsSt(fon9::LogLevel::Error, "Unknwon Add=N or A: N=Normal, A=AfterHour.");
            return false;
         }
         std::string name{"OmsTwfMiSystem_"};
         name.push_back(*value.begin());
         pthis = new OmsTwfMiSystem(omsCoreMgr, txSessionId, holder.Root_, std::move(name));
         if (!omsCoreMgr->Add(pthis)) {
            holder.SetPluginsSt(fon9::LogLevel::Error, "Cannot Add=", value);
            return false;
         }
         fon9::StrFetchTrim(value, ',');
         value = fon9::StrTrimRemoveQuotes(value);
         pthis->SetNoDataEventSch(value);

         auto* channel = pthis->GetChannel(1);
         channel->RegMiHandler<'2', '1', 4>(f9twf::ExgMiHandlerSP{new OmsTwfMiI020{*channel}});
         channel->RegMiHandler<'1', '5', 3>(f9twf::ExgMiHandlerSP{new OmsTwfMiI060{*channel}});
         channel->RegMiHandler<'2', '3', 6>(f9twf::ExgMiHandlerSP{new OmsTwfMiI140{*channel}});
         
         channel = pthis->GetChannel(2);
         channel->RegMiHandler<'5', '1', 4>(f9twf::ExgMiHandlerSP{new OmsTwfMiI020{*channel}});
         channel->RegMiHandler<'4', '5', 3>(f9twf::ExgMiHandlerSP{new OmsTwfMiI060{*channel}});
         channel->RegMiHandler<'5', '3', 6>(f9twf::ExgMiHandlerSP{new OmsTwfMiI140{*channel}});

         pthis->StartupMdSystem();
         pthis->PlantIoMgr(*holder.Root_, iomArgs);
      }
      else {
         holder.SetPluginsSt(fon9::LogLevel::Error, "Unknown tag:", tag);
         return false;
      }
   }
   return true;
}

} // namespaces
//--------------------------------------------------------------------------//
extern "C" fon9::seed::PluginsDesc f9p_OmsTwfMiSystem;
static fon9::seed::PluginsPark f9pRegister{"OmsTwfMiSystem", &f9p_OmsTwfMiSystem};
fon9::seed::PluginsDesc f9p_OmsTwfMiSystem{"",
   &f9omstw::OmsTwfMiSystem::StartupPlugins,
   nullptr,
   nullptr,
};
