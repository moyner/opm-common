/*
  Copyright 2021 Equinor ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SCHEDULE_TSTEP_HPP
#define SCHEDULE_TSTEP_HPP

#include <chrono>
#include <memory>
#include <optional>

#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>

#include <opm/parser/eclipse/EclipseState/Schedule/RPTConfig.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/PAvg.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Tuning.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/OilVaporizationProperties.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Events.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/Well.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/NameOrder.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/WListManager.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/MessageLimits.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Group/GConSump.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Group/GConSale.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Network/ExtNetwork.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/VFPProdTable.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/VFPInjTable.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Action/Actions.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/UDQ/UDQActive.hpp>

namespace Opm {

    /*
      The purpose of the ScheduleState class is to hold the entire Schedule
      information, i.e. wells and groups and so on, at exactly one point in
      time. The ScheduleState class itself has no dynamic behavior, the dynamics
      is handled by the Schedule instance owning the ScheduleState instance.
    */

    class WellTestConfig;

    class ScheduleState {
    public:
        ScheduleState() = default;
        explicit ScheduleState(const std::chrono::system_clock::time_point& start_time);
        ScheduleState(const std::chrono::system_clock::time_point& start_time, const std::chrono::system_clock::time_point& end_time);
        ScheduleState(const ScheduleState& src, const std::chrono::system_clock::time_point& start_time);
        ScheduleState(const ScheduleState& src, const std::chrono::system_clock::time_point& start_time, const std::chrono::system_clock::time_point& end_time);


        std::chrono::system_clock::time_point start_time() const;
        std::chrono::system_clock::time_point end_time() const;
        ScheduleState next(const std::chrono::system_clock::time_point& next_start);

        bool operator==(const ScheduleState& other) const;
        static ScheduleState serializeObject();

        void update_pavg(PAvg pavg);
        const PAvg& pavg() const;

        void update_tuning(Tuning tuning);
        Tuning& tuning();
        const Tuning& tuning() const;

        void update_nupcol(int nupcol);
        int nupcol() const;

        void update_oilvap(OilVaporizationProperties oilvap);
        const OilVaporizationProperties& oilvap() const;
        OilVaporizationProperties& oilvap();

        void update_events(Events events);
        Events& events();
        const Events& events() const;

        void update_wellgroup_events(WellGroupEvents wgevents);
        WellGroupEvents& wellgroup_events();
        const WellGroupEvents& wellgroup_events() const;

        void update_geo_keywords(std::vector<DeckKeyword> geo_keywords);
        std::vector<DeckKeyword>& geo_keywords();
        const std::vector<DeckKeyword>& geo_keywords() const;

        void update_message_limits(MessageLimits message_limits);
        MessageLimits& message_limits();
        const MessageLimits& message_limits() const;

        Well::ProducerCMode whistctl() const;
        void update_whistctl(Well::ProducerCMode whistctl);

        const WellTestConfig& wtest_config() const;
        void update_wtest_config(WellTestConfig wtest_config);

        const WListManager& wlist_manager() const;
        void update_wlist_manager(WListManager wlist_manager);

        const GConSale& gconsale() const;
        void update_gconsale(GConSale gconsale);

        const GConSump& gconsump() const;
        void update_gconsump(GConSump gconsump);

        const Network::ExtNetwork& network() const;
        void update_network(Network::ExtNetwork network);

        const RPTConfig& rpt_config() const;
        void update_rpt_config(RPTConfig rpt_config);

        std::vector<std::reference_wrapper<const VFPProdTable>> vfpprod() const;
        const VFPProdTable& vfpprod(int table_id) const;
        void update_vfpprod(VFPProdTable vfpprod);
        std::optional<std::reference_wrapper<const VFPProdTable>> try_vfpprod(int table_id) const;

        std::vector<std::reference_wrapper<const VFPInjTable>> vfpinj() const;
        const VFPInjTable& vfpinj(int table_id) const;
        void update_vfpinj(VFPInjTable vfpinj);
        std::optional<std::reference_wrapper<const VFPInjTable>> try_vfpinj(int table_id) const;

        const Action::Actions& actions() const;
        void update_actions(Action::Actions actions);

        const UDQActive& udq_active() const;
        void update_udq_active(UDQActive udq_active);

        const NameOrder& well_order() const;
        void well_order(const std::string& well);
        void update_well_order(NameOrder well_order);

        template<class Serializer>
        void serializeOp(Serializer& serializer) {
            serializer(m_start_time);
            serializer(m_end_time);
            serializer(m_pavg);
            m_tuning.serializeOp(serializer);
            serializer(m_nupcol);
            m_oilvap.serializeOp(serializer);
            m_events.serializeOp(serializer);
            m_wellgroup_events.serializeOp(serializer);
            serializer.vector(m_geo_keywords);
            m_message_limits.serializeOp(serializer);
            serializer(m_well_order);
            serializer(m_whistctl_mode);
            serializer.map(m_vfpprod);
            serializer.map(m_vfpinj);
        }


    private:
        std::chrono::system_clock::time_point m_start_time;
        std::optional<std::chrono::system_clock::time_point> m_end_time;

        std::shared_ptr<PAvg> m_pavg;
        Tuning m_tuning;
        int m_nupcol;
        OilVaporizationProperties m_oilvap;
        Events m_events;
        WellGroupEvents m_wellgroup_events;
        std::vector<DeckKeyword> m_geo_keywords;
        MessageLimits m_message_limits;
        Well::ProducerCMode m_whistctl_mode = Well::ProducerCMode::CMODE_UNDEFINED;
        std::shared_ptr<NameOrder> m_well_order;
        std::shared_ptr<WellTestConfig> m_wtest_config;
        std::shared_ptr<GConSale> m_gconsale;
        std::shared_ptr<GConSump> m_gconsump;
        std::shared_ptr<WListManager> m_wlist_manager;
        std::shared_ptr<Network::ExtNetwork> m_network;
        std::shared_ptr<RPTConfig> m_rptconfig;
        std::shared_ptr<Action::Actions> m_actions;
        std::shared_ptr<UDQActive> m_udq_active;
        std::map<int, std::shared_ptr<VFPProdTable>> m_vfpprod;
        std::map<int, std::shared_ptr<VFPInjTable>> m_vfpinj;
    };
}

#endif
