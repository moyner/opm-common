/*
  Copyright 2016 Statoil ASA.

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
#include <fnmatch.h>

#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <opm/parser/eclipse/Parser/ParseContext.hpp>
#include <opm/parser/eclipse/Parser/ErrorGuard.hpp>
#include <opm/common/utility/OpmInputError.hpp>

#include <opm/parser/eclipse/Deck/Deck.hpp>
#include <opm/parser/eclipse/Deck/DeckItem.hpp>
#include <opm/parser/eclipse/Deck/DeckKeyword.hpp>
#include <opm/parser/eclipse/Deck/DeckRecord.hpp>
#include <opm/parser/eclipse/Deck/DeckSection.hpp>

#include <opm/parser/eclipse/EclipseState/EclipseState.hpp>
#include <opm/parser/eclipse/EclipseState/Tables/TableManager.hpp>
#include <opm/parser/eclipse/EclipseState/AquiferConfig.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridDims.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Group/Group.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Network/ExtNetwork.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/UDQ/UDQConfig.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Schedule.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/TimeMap.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/Connection.hpp>
#include <opm/parser/eclipse/EclipseState/Schedule/Well/WellConnections.hpp>
#include <opm/parser/eclipse/EclipseState/SummaryConfig/SummaryConfig.hpp>


namespace Opm {

namespace {

    const std::vector<std::string> ALL_keywords = {
        "FAQR",  "FAQRG", "FAQT", "FAQTG", "FGIP", "FGIPG", "FGIPL",
        "FGIR",  "FGIT",  "FGOR", "FGPR",  "FGPT", "FOIP",  "FOIPG",
        "FOIPL", "FOIR",  "FOIT", "FOPR",  "FOPT", "FPR",   "FVIR",
        "FVIT",  "FVPR",  "FVPT", "FWCT",  "FWGR", "FWIP",  "FWIR",
        "FWIT",  "FWPR",  "FWPT",
        "GGIR",  "GGIT",  "GGOR", "GGPR",  "GGPT", "GOIR",  "GOIT",
        "GOPR",  "GOPT",  "GVIR", "GVIT",  "GVPR", "GVPT",  "GWCT",
        "GWGR",  "GWIR",  "GWIT", "GWPR",  "GWPT",
        "WBHP",  "WGIR",  "WGIT", "WGOR",  "WGPR", "WGPT",  "WOIR",
        "WOIT",  "WOPR",  "WOPT", "WPI",   "WTHP", "WVIR",  "WVIT",
        "WVPR",  "WVPT",  "WWCT", "WWGR",  "WWIR", "WWIT",  "WWPR",
        "WWPT",  "WGLIR",
        // ALL will not expand to these keywords yet
        // Analytical aquifer keywords
        "AAQR",  "AAQRG", "AAQT", "AAQTG"
    };

    const std::vector<std::string> GMWSET_keywords = {
        "GMWPT", "GMWPR", "GMWPA", "GMWPU", "GMWPG", "GMWPO", "GMWPS",
        "GMWPV", "GMWPP", "GMWPL", "GMWIT", "GMWIN", "GMWIA", "GMWIU", "GMWIG",
        "GMWIS", "GMWIV", "GMWIP", "GMWDR", "GMWDT", "GMWWO", "GMWWT"
    };

    const std::vector<std::string> FMWSET_keywords = {
        "FMCTF", "FMWPT", "FMWPR", "FMWPA", "FMWPU", "FMWPF", "FMWPO", "FMWPS",
        "FMWPV", "FMWPP", "FMWPL", "FMWIT", "FMWIN", "FMWIA", "FMWIU", "FMWIF",
        "FMWIS", "FMWIV", "FMWIP", "FMWDR", "FMWDT", "FMWWO", "FMWWT"
    };


    const std::vector<std::string> PERFORMA_keywords = {
        "TCPU", "ELAPSED","NEWTON","NLINEARS","NLINSMIN", "NLINSMAX","MLINEARS",
        "MSUMLINS","MSUMNEWT","TIMESTEP","TCPUTS","TCPUDAY","STEPTYPE","TELAPLIN"
    };

    const std::vector<std::string> DATE_keywords = {
         "DAY", "MONTH", "YEAR"
    };


    /*
      The variable type 'ECL_SMSPEC_MISC_TYPE' is a catch-all variable
      type, and will by default internalize keywords like 'ALL' and
      'PERFORMA', where only the keywords in the expanded list should
      be included.
    */
    const std::map<std::string, std::vector<std::string>> meta_keywords = {{"PERFORMA", PERFORMA_keywords},
                                                                           {"DATE", DATE_keywords},
                                                                           {"ALL", ALL_keywords},
                                                                           {"FMWSET", FMWSET_keywords},
                                                                           {"GMWSET", GMWSET_keywords}};

    /*
      This is a hardcoded mapping between 3D field keywords,
      e.g. 'PRESSURE' and 'SWAT' and summary keywords like 'RPR' and
      'BPR'. The purpose of this mapping is to maintain an overview of
      which 3D field keywords are needed by the Summary calculation
      machinery, based on which summary keywords are requested. The
      Summary calculations are implemented in the opm-output
      repository.
    */
    const std::map<std::string , std::set<std::string>> required_fields =  {
         {"PRESSURE", {"FPR" , "RPR" , "BPR"}},
         {"OIP"  , {"ROIP" , "FOIP" , "FOE"}},
         {"OIPL" , {"ROIPL" ,"FOIPL" }},
         {"OIPG" , {"ROIPG" ,"FOIPG"}},
         {"GIP"  , {"RGIP" , "FGIP"}},
         {"GIPL" , {"RGIPL" , "FGIPL"}},
         {"GIPG" , {"RGIPG", "FGIPG"}},
         {"WIP"  , {"RWIP" , "FWIP"}},
         {"SWAT" , {"BSWAT"}},
         {"SGAS" , {"BSGAS"}}
    };

    using keyword_set = std::unordered_set<std::string>;

    inline bool is_in_set(const keyword_set& set, const std::string& keyword) {
        return set.find(keyword) != set.end();
    }

    bool is_special(const std::string& keyword) {
        static const keyword_set specialkw {
            "ELAPSED",
            "MAXDPR",
            "MAXDSG",
            "MAXDSO",
            "MAXDSW",
            "NAIMFRAC",
            "NEWTON",
            "NLINEARS",
            "NLINSMAX",
            "NLINSMIN",
            "STEPTYPE",
            "WNEWTON",
        };

        return is_in_set(specialkw, keyword);
    }

    bool is_udq_blacklist(const std::string& keyword) {
        static const keyword_set udq_blacklistkw {
            "SUMTHIN",
        };

        return is_in_set(udq_blacklistkw, keyword);
    }

    bool is_processing_instruction(const std::string& keyword) {
        static const keyword_set processing_instructionkw {
            "NARROW",
            "RPTONLY",
            "RUNSUM",
            "SEPARATE",
            "SUMMARY",
        };

        return is_in_set(processing_instructionkw, keyword);
    }

    bool is_udq(const std::string& keyword) {
        // Does 'keyword' match one of the patterns
        //   AU*, BU*, CU*, FU*, GU*, RU*, SU*, or WU*?
        using sz_t = std::string::size_type;
        return (keyword.size() > sz_t{1})
            && (keyword[1] == 'U')
            && !is_udq_blacklist(keyword)
            && (keyword.find_first_of("WGFCRBSA") == sz_t{0});
    }

    bool is_pressure(const std::string& keyword) {
        static const keyword_set presskw {
            "BHP", "BHPH", "THP", "THPH", "PR", "PRD", "PRDH", "PRDF", "PRDA",
        };

        return is_in_set(presskw, keyword.substr(1));
    }

    bool is_rate(const std::string& keyword) {
        static const keyword_set ratekw {
            "OPR", "GPR", "WPR", "GLIR", "LPR", "NPR", "VPR",
            "OPGR", "GPGR", "WPGR", "VPGR",
            "OPRH", "GPRH", "WPRH", "LPRH",
            "OVPR", "GVPR", "WVPR",
            "OPRS", "GPRS", "OPRF", "GPRF",

            "OIR", "GIR", "WIR", "LIR", "NIR", "VIR",
            "OIGR", "GIGR", "WIGR",
            "OIRH", "GIRH", "WIRH",
            "OVIR", "GVIR", "WVIR",

            "OPI", "OPP", "GPI", "GPP", "WPI", "WPP",
        };

        return is_in_set(ratekw, keyword.substr(1));
    }

    bool is_ratio(const std::string& keyword) {
        static const keyword_set ratiokw {
            "GLR", "GOR", "WCT",
            "GLRH", "GORH", "WCTH",
        };

        return is_in_set(ratiokw, keyword.substr(1));
    }

    bool is_total(const std::string& keyword) {
        static const keyword_set totalkw {
            "OPT", "GPT", "WPT", "LPT", "NPT",
            "VPT", "OVPT", "GVPT", "WVPT",
            "WPTH", "OPTH", "GPTH", "LPTH",
            "GPTS", "OPTS", "GPTF", "OPTF",

            "WIT", "OIT", "GIT", "LIT", "NIT", "CIT", "VIT",
            "WITH", "OITH", "GITH", "WVIT", "OVIT", "GVIT",
        };

        return is_in_set(totalkw, keyword.substr(1));
    }

    bool is_count(const std::string& keyword) {
        static const keyword_set countkw {
            "MWIN", "MWIT", "MWPR", "MWPT"
        };

        return is_in_set(countkw, keyword);
    }

    bool is_control_mode(const std::string& keyword) {
        static const keyword_set countkw {
            "MCTP", "MCTW", "MCTG"
        };

        return (keyword == "WMCTL")
            || is_in_set(countkw, keyword.substr(1));
    }

    bool is_liquid_phase(const std::string& keyword) {
        return keyword == "WPIL";
    }

    bool is_region_to_region(const std::string& keyword) {
        using sz_t = std::string::size_type;
        if ((keyword.size() == sz_t{3}) && keyword[2] == 'F') return true;
        if ((keyword == "RNLF") || (keyword == "RORFR")) return true;
        if ((keyword.size() >= sz_t{4}) && ((keyword[2] == 'F') && ((keyword[3] == 'T') || (keyword[3] == 'R')))) return true;
        if ((keyword.size() >= sz_t{5}) && ((keyword[3] == 'F') && ((keyword[4] == 'T') || (keyword[4] == 'R')))) return true;

        return false;
    }

    bool is_aquifer(const std::string& keyword)
    {
        return (keyword[0] == 'A') && (keyword != "ALL");
    }

    bool is_node_keyword(const std::string& keyword)
    {
        static const auto nodekw = keyword_set {
            "GPR", "GPRG", "GPRW",
        };

        return is_in_set(nodekw, keyword);
    }

    bool need_node_names(const SUMMARYSection& sect)
    {
        // We need the the node names if there is any node-related summary
        // keywords in the input deck's SUMMARY section.  The reason is that
        // we need to be able to fill out all node names in the case of a
        // keyword that does not specify any nodes (e.g., "GPR /"), and to
        // check for missing nodes if a keyword is erroneously specified.

        return std::any_of(sect.begin(), sect.end(),
            [](const DeckKeyword& keyword)
        {
            return is_node_keyword(keyword.name());
        });
    }

    std::vector<std::string> collect_node_names(const Schedule& sched)
    {
        auto node_names = std::vector<std::string>{};
        auto names = std::unordered_set<std::string>{};

        const auto nstep = sched.getTimeMap().numTimesteps();
        for (auto step = 0*nstep; step < nstep; ++step) {
            const auto& nodes = sched.network(step).node_names();
            names.insert(nodes.begin(), nodes.end());
        }

        node_names.assign(names.begin(), names.end());
        std::sort(node_names.begin(), node_names.end());

        return node_names;
    }

    SummaryConfigNode::Category
    distinguish_group_from_node(const std::string& keyword)
    {
        return is_node_keyword(keyword)
            ? SummaryConfigNode::Category::Node
            : SummaryConfigNode::Category::Group;
    }


void handleMissingWell( const ParseContext& parseContext, ErrorGuard& errors, const KeywordLocation& location, const std::string& well) {
    std::string msg_fmt = fmt::format("Request for missing well {} in {{keyword}}\n"
                                      "In {{file}} line {{line}}", well);
    parseContext.handleError( ParseContext::SUMMARY_UNKNOWN_WELL , msg_fmt, location, errors );
}


void handleMissingGroup( const ParseContext& parseContext , ErrorGuard& errors, const KeywordLocation& location, const std::string& group) {
    std::string msg_fmt = fmt::format("Request for missing group {} in {{keyword}}\n"
                                      "In {{file}} line {{line}}", group);
    parseContext.handleError( ParseContext::SUMMARY_UNKNOWN_GROUP , msg_fmt, location, errors );
}

void handleMissingNode( const ParseContext& parseContext, ErrorGuard& errors, const KeywordLocation& location, const std::string& node_name )
{
    std::string msg_fmt = fmt::format("Request for missing network node {} in {{keyword}}\n"
                                      "In {{file}} line {{line}}", node_name);
    parseContext.handleError( ParseContext::SUMMARY_UNKNOWN_NODE, msg_fmt, location, errors );
}

void handleMissingAquifer( const ParseContext& parseContext, ErrorGuard& errors, const KeywordLocation& location, const int id) {
    std::string msg_fmt = fmt::format("Request for missing aquifer {} in {{keyword}}\n"
                                      "In {{file}} line {{line}}", id);
    parseContext.handleError(ParseContext::SUMMARY_UNKNOWN_AQUIFER, msg_fmt, location, errors);
}

inline void keywordW( SummaryConfig::keyword_list& list,
                      const std::vector<std::string>& well_names,
                      SummaryConfigNode baseWellParam) {
    for (const auto& wname : well_names)
        list.push_back( baseWellParam.namedEntity(wname) );
}

inline void keywordAquifer( SummaryConfig::keyword_list& list,
                            const AquiferConfig& aquiferConfig,
                            SummaryConfigNode baseAquiferParam) {
    if ( !aquiferConfig.active() ) return;

    for (const auto& aq : aquiferConfig.ct()) {
        list.push_back(baseAquiferParam.number(aq.aquiferID));
    }
    for (const auto& aq : aquiferConfig.fetp()) {
        list.push_back(baseAquiferParam.number(aq.aquiferID));
    }
}
// later check whether parseContext and errors are required
// maybe loc will be needed
inline void keywordAquifer( SummaryConfig::keyword_list& list,
                            const AquiferConfig& aquiferConfig,
                            const ParseContext& parseContext,
                            ErrorGuard& errors,
                            const DeckKeyword& keyword) {
    auto param = SummaryConfigNode {
        keyword.name(), SummaryConfigNode::Category::Aquifer, keyword.location()
    }
    .parameterType( parseKeywordType(keyword.name()) )
    .isUserDefined( is_udq(keyword.name()) );

    if (keyword.size() && keyword.getDataRecord().getDataItem().hasValue(0)) {
        for( const int id: keyword.getIntData()) {
            if (aquiferConfig.hasAquifer(id)) {
                list.push_back(param.number(id));
            } else {
                handleMissingAquifer(parseContext, errors, keyword.location(), id);
            }
        }
    } else {
        keywordAquifer(list, aquiferConfig, param);
    }
}

inline void keywordW( SummaryConfig::keyword_list& list,
                      const std::string& keyword,
                      KeywordLocation loc,
                      const Schedule& schedule) {
    auto param = SummaryConfigNode {
        keyword, SummaryConfigNode::Category::Well , std::move(loc)
    }
    .parameterType( parseKeywordType(keyword) )
    .isUserDefined( is_udq(keyword) );

    keywordW( list, schedule.wellNames(), param );
}


inline void keywordW( SummaryConfig::keyword_list& list,
                      const ParseContext& parseContext,
                      ErrorGuard& errors,
                      const DeckKeyword& keyword,
                      const Schedule& schedule ) {
    /*
      Two step check for whether to discard this keyword as unsupported:

      1. Completion quantity keywords are currently not supported.  These are
      well summary keywords, apart from "WMCTL" and "WPIL", that end in 'L'.

      2. If the keyword is a UDQ keyword there is no convention enforced to
      the last character, and in that case it is treated as a normal well
      keyword anyways.
    */
    if (keyword.name().back() == 'L') {
        if (! (is_control_mode(keyword.name()) || is_liquid_phase(keyword.name()) || is_udq(keyword.name()))) {
            const auto& location = keyword.location();
            std::string msg = "Unsupported summary output keyword {}\n"
                              "In {file} line {line}";
            parseContext.handleError( ParseContext::SUMMARY_UNHANDLED_KEYWORD, msg, location, errors);
            return;
        }
    }

    auto param = SummaryConfigNode {
        keyword.name(), SummaryConfigNode::Category::Well, keyword.location()
    }
    .parameterType( parseKeywordType(keyword.name()) )
    .isUserDefined( is_udq(keyword.name()) );

    if (keyword.size() && keyword.getDataRecord().getDataItem().hasValue(0)) {
        for( const std::string& pattern : keyword.getStringData()) {
          auto well_names = schedule.wellNames( pattern, schedule.size() - 1 );

            if( well_names.empty() )
                handleMissingWell( parseContext, errors, keyword.location(), pattern );

            keywordW( list, well_names, param );
        }
    } else
        keywordW( list, schedule.wellNames(), param );
}

inline void keywordG( SummaryConfig::keyword_list& list,
                      const std::string& keyword,
                      KeywordLocation loc,
                      const Schedule& schedule ) {
    auto param = SummaryConfigNode {
        keyword, SummaryConfigNode::Category::Group, std::move(loc)
    }
    .parameterType( parseKeywordType(keyword) )
    .isUserDefined( is_udq(keyword) );

    for( const auto& group : schedule.groupNames() ) {
        if( group == "FIELD" ) continue;
        list.push_back( param.namedEntity(group) );
    }
}


inline void keywordG( SummaryConfig::keyword_list& list,
                      const ParseContext& parseContext,
                      ErrorGuard& errors,
                      const DeckKeyword& keyword,
                      const Schedule& schedule ) {

    if( keyword.name() == "GMWSET" ) return;

    auto param = SummaryConfigNode {
        keyword.name(), SummaryConfigNode::Category::Group, keyword.location()
    }
    .parameterType( parseKeywordType(keyword.name()) )
    .isUserDefined( is_udq(keyword.name()) );

    if( keyword.size() == 0 ||
        !keyword.getDataRecord().getDataItem().hasValue( 0 ) ) {

        for( const auto& group : schedule.groupNames() ) {
            if( group == "FIELD" ) continue;
            list.push_back( param.namedEntity(group) );
        }
        return;
    }

    const auto& item = keyword.getDataRecord().getDataItem();

    for( const std::string& group : item.getData< std::string >() ) {
        if( schedule.hasGroup( group ) )
            list.push_back( param.namedEntity(group) );
        else
            handleMissingGroup( parseContext, errors, keyword.location(), group );
    }
}

void keyword_node( SummaryConfig::keyword_list& list,
                   const std::vector<std::string>& node_names,
                   const ParseContext& parseContext,
                   ErrorGuard& errors,
                   const DeckKeyword& keyword)
{
    if (node_names.empty()) {
        const auto& location = keyword.location();
        std::string msg = "The network node keyword {keyword} is not supported in runs without networks\n"
                          "In {file} line {line}";
        parseContext.handleError( ParseContext::SUMMARY_UNHANDLED_KEYWORD, msg, location, errors);
        return;
    }

    auto param = SummaryConfigNode {
        keyword.name(), SummaryConfigNode::Category::Node, keyword.location()
    }
    .parameterType( parseKeywordType(keyword.name()) )
    .isUserDefined( is_udq(keyword.name()) );

    if( keyword.size() == 0 ||
        !keyword.getDataRecord().getDataItem().hasValue( 0 ) ) {

        for (const auto& node_name : node_names) {
            list.push_back( param.namedEntity(node_name) );
        }

        return;
    }

    const auto& item = keyword.getDataRecord().getDataItem();

    for (const auto& node_name : item.getData<std::string>()) {
        auto pos = std::find(node_names.begin(),
                             node_names.end(), node_name);
        if (pos != node_names.end())
            list.push_back( param.namedEntity(node_name) );
        else
            handleMissingNode( parseContext, errors, keyword.location(), node_name );
    }
}

inline void keywordF( SummaryConfig::keyword_list& list,
                      const std::string& keyword,
                      KeywordLocation loc) {
    auto param = SummaryConfigNode {
        keyword, SummaryConfigNode::Category::Field, std::move(loc)
    }
    .parameterType( parseKeywordType(keyword) )
    .isUserDefined( is_udq(keyword) );

    list.push_back( std::move(param) );
}

inline void keywordAquifer( SummaryConfig::keyword_list& list,
                            const std::string& keyword,
                            const AquiferConfig& aquiferConfig,
                            KeywordLocation loc) {
    auto param = SummaryConfigNode {
            keyword, SummaryConfigNode::Category::Aquifer, std::move(loc)
    }
            .parameterType( parseKeywordType(keyword) )
            .isUserDefined( is_udq(keyword) );

    keywordAquifer(list, aquiferConfig, param);
}

inline void keywordF( SummaryConfig::keyword_list& list,
                      const DeckKeyword& keyword ) {
    if( keyword.name() == "FMWSET" ) return;
    keywordF( list, keyword.name(), keyword.location() );
}

inline std::array< int, 3 > getijk( const DeckRecord& record,
                                    int offset = 0 ) {
    return {{
        record.getItem( offset + 0 ).get< int >( 0 ) - 1,
        record.getItem( offset + 1 ).get< int >( 0 ) - 1,
        record.getItem( offset + 2 ).get< int >( 0 ) - 1
    }};
}

inline std::array< int, 3 > getijk( const Connection& completion ) {
    return { { completion.getI(), completion.getJ(), completion.getK() }};
}


inline void keywordB( SummaryConfig::keyword_list& list,
                      const DeckKeyword& keyword,
                      const GridDims& dims) {
    auto param = SummaryConfigNode {
        keyword.name(), SummaryConfigNode::Category::Block, keyword.location()
    }
    .parameterType( parseKeywordType(keyword.name()) )
    .isUserDefined( is_udq(keyword.name()) );

  for( const auto& record : keyword ) {
      auto ijk = getijk( record );
      int global_index = 1 + dims.getGlobalIndex(ijk[0], ijk[1], ijk[2]);
      list.push_back( param.number(global_index) );
  }
}

inline void keywordR2R( SummaryConfig::keyword_list& /* list */,
                        const ParseContext& parseContext,
                        ErrorGuard& errors,
                        const DeckKeyword& keyword)
{
    const auto& location = keyword.location();
    std::string msg_fmt = "Region to region summary keyword {keyword} is ignored\n"
                          "In {file} line {line}";
    parseContext.handleError(ParseContext::SUMMARY_UNHANDLED_KEYWORD, msg_fmt, location, errors);
}


inline void keywordR( SummaryConfig::keyword_list& list,
                      const DeckKeyword& deck_keyword,
                      const TableManager& tables,
                      const ParseContext& parseContext,
                      ErrorGuard& errors ) {

    auto keyword = deck_keyword.name();
    if( is_region_to_region(keyword) ) {
        keywordR2R( list, parseContext, errors, deck_keyword );
        return;
    }
    std::string region_name = "FIPNUM";
    if (keyword.size() > 5)
        region_name = "FIP" + keyword.substr(5,3);

    const size_t numfip = tables.numFIPRegions( );
    const auto& item = deck_keyword.getDataRecord().getDataItem();
    std::vector<int> regions;

    if (item.data_size() > 0)
        regions = item.getData< int >();
    else {
        for (size_t region=1; region <= numfip; region++)
            regions.push_back( region );
    }

    auto param = SummaryConfigNode {
        keyword, SummaryConfigNode::Category::Region, deck_keyword.location()
    }
    .fip_region( region_name )
    .isUserDefined( is_udq(keyword) );

    for( const int region : regions ) {
        if (region >= 1 && region <= static_cast<int>(numfip))
            list.push_back( param.number( region ) );
        else
            throw std::invalid_argument("Illegal region value: " + std::to_string( region ));
    }
}


inline void keywordMISC( SummaryConfig::keyword_list& list,
                         const std::string& keyword,
                         KeywordLocation loc)
{
    if (meta_keywords.find(keyword) == meta_keywords.end())
        list.emplace_back( keyword, SummaryConfigNode::Category::Miscellaneous , std::move(loc));
}

inline void keywordMISC( SummaryConfig::keyword_list& list,
                         const DeckKeyword& keyword)
{
    keywordMISC(list, keyword.name(), keyword.location());
}

  inline void keywordC( SummaryConfig::keyword_list& list,
                        const ParseContext& parseContext,
                        ErrorGuard& errors,
                        const DeckKeyword& keyword,
                        const Schedule& schedule,
                        const GridDims& dims) {

    auto param = SummaryConfigNode {
        keyword.name(), SummaryConfigNode::Category::Connection, keyword.location()
    }
    .parameterType( parseKeywordType( keyword.name()) )
    .isUserDefined( is_udq(keyword.name()) );

    for( const auto& record : keyword ) {

        const auto& wellitem = record.getItem( 0 );

        const auto well_names = wellitem.defaultApplied( 0 )
                              ? schedule.wellNames()
                              : schedule.wellNames( wellitem.getTrimmedString( 0 ) );
        const auto ijk_defaulted = record.getItem( 1 ).defaultApplied( 0 );

        if( well_names.empty() )
            handleMissingWell( parseContext, errors, keyword.location(), wellitem.getTrimmedString( 0 ) );

        for(const auto& name : well_names) {
            param.namedEntity(name);
            const auto& well = schedule.getWellatEnd(name);
            /*
             * we don't want to add completions that don't exist, so we iterate
             * over a well's completions regardless of the desired block is
             * defaulted or not
             */
            for( const auto& connection : well.getConnections() ) {
                auto cijk = getijk( connection );
                int global_index = 1 + dims.getGlobalIndex(cijk[0], cijk[1], cijk[2]);

                if( ijk_defaulted || ( cijk == getijk(record, 1) ) )
                    list.push_back( param.number(global_index) );
            }
        }
    }
}

    bool isKnownSegmentKeyword(const DeckKeyword& keyword)
    {
        const auto& kw = keyword.name();

        if (kw.size() > 5) {
            // Easy check first--handles SUMMARY and SUMTHIN &c.
            return false;
        }

        const auto kw_whitelist = std::vector<const char*> {
            "SOFR", "SGFR", "SWFR", "SPR", "SPRD", "SPRDH", "SPRDF", "SPRDA",
        };

        return std::any_of(kw_whitelist.begin(), kw_whitelist.end(),
                           [&kw](const char* known) -> bool
                           {
                               return kw == known;
                           });
    }


    int maxNumWellSegments(const std::size_t /* last_timestep */,
                           const Well&       well)
    {
        return well.isMultiSegment()
            ? well.getSegments().size() : 0;
    }

    void makeSegmentNodes(const std::size_t               last_timestep,
                          const int                       segID,
                          const DeckKeyword&              keyword,
                          const Well&                    well,
                          SummaryConfig::keyword_list&    list)
    {
        if (!well.isMultiSegment())
            // Not an MSW.  Don't create summary vectors for segments.
            return;

        auto param = SummaryConfigNode {
            keyword.name(), SummaryConfigNode::Category::Segment, keyword.location()
        }
        .namedEntity( well.name() )
        .isUserDefined( is_udq(keyword.name()) );

        if (segID < 1) {
            // Segment number defaulted.  Allocate a summary
            // vector for each segment.
            const auto nSeg = maxNumWellSegments(last_timestep, well);

            for (auto segNumber = 0*nSeg; segNumber < nSeg; ++segNumber)
                list.push_back( param.number(segNumber + 1) );
        }
        else
            // Segment number specified.  Allocate single
            // summary vector for that segment number.
            list.push_back( param.number(segID) );
    }

    void keywordSNoRecords(const std::size_t            last_timestep,
                           const DeckKeyword&           keyword,
                           const Schedule&              schedule,
                           SummaryConfig::keyword_list& list)
    {
        // No keyword records.  Allocate summary vectors for all
        // segments in all wells at all times.
        //
        // Expected format:
        //
        //   SGFR
        //   / -- All segments in all MS wells at all times.

        const auto segID = -1;

        for (const auto& well : schedule.getWellsatEnd())
            makeSegmentNodes(last_timestep, segID, keyword,
                             well, list);
    }

    void keywordSWithRecords(const std::size_t            last_timestep,
                             const ParseContext&          parseContext,
                             ErrorGuard& errors,
                             const DeckKeyword&           keyword,
                             const Schedule&              schedule,
                             SummaryConfig::keyword_list& list)
    {
        // Keyword has explicit records.  Process those and create
        // segment-related summary vectors for those wells/segments
        // that match the description.
        //
        // Expected formats:
        //
        //   SOFR
        //     'W1'   1 /
        //     'W1'  10 /
        //     'W3'     / -- All segments
        //   /
        //
        //   SPR
        //     1*   2 / -- Segment 2 in all multi-segmented wells
        //   /

        for (const auto& record : keyword) {
            const auto& wellitem = record.getItem(0);
            const auto& well_names = wellitem.defaultApplied(0)
                ? schedule.wellNames()
                : schedule.wellNames(wellitem.getTrimmedString(0));

            if (well_names.empty())
                handleMissingWell(parseContext, errors, keyword.location(),
                                  wellitem.getTrimmedString(0));

            // Negative 1 (< 0) if segment ID defaulted.  Defaulted
            // segment number in record implies all segments.
            const auto segID = record.getItem(1).defaultApplied(0)
                ? -1 : record.getItem(1).get<int>(0);

            for (const auto& well_name : well_names)
                makeSegmentNodes(last_timestep, segID, keyword, schedule.getWellatEnd(well_name), list);
        }
    }

    inline void keywordS(SummaryConfig::keyword_list& list,
                         const ParseContext&          parseContext,
                         ErrorGuard& errors,
                         const DeckKeyword&           keyword,
                         const Schedule&              schedule)
    {
        // Generate SMSPEC nodes for SUMMARY keywords of the form
        //
        //   SOFR
        //     'W1'   1 /
        //     'W1'  10 /
        //     'W3'     / -- All segments
        //   /
        //
        //   SPR
        //     1*   2 / -- Segment 2 in all multi-segmented wells
        //   /
        //
        //   SGFR
        //   / -- All segments in all MS wells at all times.

        if (! isKnownSegmentKeyword(keyword)) {
            // Ignore keywords that have not been explicitly white-listed
            // for treatment as segment summary vectors.
            return;
        }

        const auto last_timestep = schedule.getTimeMap().last();

        if (keyword.size() > 0) {
            // Keyword with explicit records.
            // Handle as alternatives SOFR and SPR above
            keywordSWithRecords(last_timestep, parseContext, errors,
                                keyword, schedule, list);
        }
        else {
            // Keyword with no explicit records.
            // Handle as alternative SGFR above.
            keywordSNoRecords(last_timestep, keyword, schedule, list);
        }
    }

    std::string to_string(const SummaryConfigNode::Category cat) {
        switch( cat ) {
            case SummaryConfigNode::Category::Aquifer: return "Aquifer";
            case SummaryConfigNode::Category::Well: return "Well";
            case SummaryConfigNode::Category::Group: return "Group";
            case SummaryConfigNode::Category::Field: return "Field";
            case SummaryConfigNode::Category::Region: return "Region";
            case SummaryConfigNode::Category::Block: return "Block";
            case SummaryConfigNode::Category::Connection: return "Connection";
            case SummaryConfigNode::Category::Segment: return "Segment";
            case SummaryConfigNode::Category::Node: return "Node";
            case SummaryConfigNode::Category::Miscellaneous: return "Miscellaneous";
        }

        throw std::invalid_argument {
            "Unhandled Summary Parameter Category '"
            + std::to_string(static_cast<int>(cat)) + '\''
        };
    }

    void check_udq( const KeywordLocation& location,
                    const Schedule& schedule,
                    const ParseContext& parseContext,
                    ErrorGuard& errors ) {
        if (! is_udq(location.keyword))
            // Nothing to do
            return;

        const auto& udq = schedule.getUDQConfig(schedule.size() - 1);

        if (!udq.has_keyword(location.keyword)) {
            std::string msg = "Summary output requested for UDQ {keyword}\n"
                              "In {file} line {line}\n"
                              "No defintion for this UDQ found in the SCHEDULE section";
            parseContext.handleError(ParseContext::SUMMARY_UNDEFINED_UDQ, msg, location, errors);
            return;
        }

        if (!udq.has_unit(location.keyword)) {
            std::string msg = "Summary output requested for UDQ {keyword}\n"
                              "In {file} line {line}\n"
                              "No unit defined in the SCHEDULE section for {keyword}";
            parseContext.handleError(ParseContext::SUMMARY_UDQ_MISSING_UNIT, msg, location, errors);
        }
    }

  inline void handleKW( SummaryConfig::keyword_list& list,
                        const std::vector<std::string>& node_names,
                        const DeckKeyword& keyword,
                        const Schedule& schedule,
                        const TableManager& tables,
                        const AquiferConfig& aquiferConfig,
                        const ParseContext& parseContext,
                        ErrorGuard& errors,
                        const GridDims& dims) {
    using Cat = SummaryConfigNode::Category;

    const auto& name = keyword.name();
    check_udq( keyword.location(), schedule, parseContext, errors );

    const auto cat = parseKeywordCategory( name );
    switch( cat ) {
        case Cat::Well: return keywordW( list, parseContext, errors, keyword, schedule );
        case Cat::Group: return keywordG( list, parseContext, errors, keyword, schedule );
        case Cat::Field: return keywordF( list, keyword );
        case Cat::Block: return keywordB( list, keyword, dims );
        case Cat::Region: return keywordR( list, keyword, tables, parseContext, errors );
        case Cat::Connection: return keywordC( list, parseContext, errors, keyword, schedule, dims);
        case Cat::Segment: return keywordS( list, parseContext, errors, keyword, schedule );
        case Cat::Node: return keyword_node( list, node_names, parseContext, errors, keyword );
        case Cat::Aquifer: return keywordAquifer(list, aquiferConfig, parseContext, errors, keyword);
        case Cat::Miscellaneous: return keywordMISC( list, keyword );

        default:
            std::string msg_fmt = fmt::format("Summary output keyword {{keyword}} of type {} is not supported\n"
                                              "In {{file}} line {{line}}", to_string(cat));
            parseContext.handleError(ParseContext::SUMMARY_UNHANDLED_KEYWORD, msg_fmt, keyword.location(), errors);
            return;
    }
}

inline void handleKW( SummaryConfig::keyword_list& list,
                      const std::string& keyword,
                      const KeywordLocation& location,
                      const Schedule& schedule,
                      const AquiferConfig& aquiferConfig,
                      const ParseContext& /* parseContext */,
                      ErrorGuard& /* errors */) {


    if (is_udq(keyword))
        throw std::logic_error("UDQ keywords not handleded when expanding alias list");

    using Cat = SummaryConfigNode::Category;
    const auto cat = parseKeywordCategory( keyword );

    switch( cat ) {
        case Cat::Well: return keywordW( list, keyword, location, schedule );
        case Cat::Group: return keywordG( list, keyword, location, schedule );
        case Cat::Field: return keywordF( list, keyword, location );
        case Cat::Aquifer: return keywordAquifer( list, keyword, aquiferConfig, location );
        case Cat::Miscellaneous: return keywordMISC( list, keyword, location);

        default:
            throw std::logic_error("Keyword type: " + to_string( cat ) + " is not supported in alias lists. Internal error handling: " + keyword);
    }
}


  inline void uniq( SummaryConfig::keyword_list& vec ) {
    std::sort( vec.begin(), vec.end() );
    auto logical_end = std::unique( vec.begin(), vec.end() );
    vec.erase( logical_end, vec.end() );
  }
}

// =====================================================================

SummaryConfigNode::Type parseKeywordType(const std::string& keyword) {
    if (is_rate(keyword)) return SummaryConfigNode::Type::Rate;
    if (is_total(keyword)) return SummaryConfigNode::Type::Total;
    if (is_ratio(keyword)) return SummaryConfigNode::Type::Ratio;
    if (is_pressure(keyword)) return SummaryConfigNode::Type::Pressure;
    if (is_count(keyword)) return SummaryConfigNode::Type::Count;
    if (is_control_mode(keyword)) return SummaryConfigNode::Type::Mode;

    return SummaryConfigNode::Type::Undefined;
}

SummaryConfigNode::Category parseKeywordCategory(const std::string& keyword) {
    using Cat = SummaryConfigNode::Category;

    if (is_special(keyword)) { return Cat::Miscellaneous; }

    switch (keyword[0]) {
        case 'A': if (is_aquifer(keyword)) return Cat::Aquifer; break;
        case 'W': return Cat::Well;
        case 'G': return distinguish_group_from_node(keyword);
        case 'F': return Cat::Field;
        case 'C': return Cat::Connection;
        case 'R': return Cat::Region;
        case 'B': return Cat::Block;
        case 'S': return Cat::Segment;
    }

    // TCPU, MLINEARS, NEWTON, &c
    return Cat::Miscellaneous;
}


SummaryConfigNode::SummaryConfigNode(std::string keyword, const Category cat, KeywordLocation loc_arg) :
    keyword_(std::move(keyword)),
    category_(cat),
    loc(std::move(loc_arg))
{}

SummaryConfigNode SummaryConfigNode::serializeObject()
{
    SummaryConfigNode result;
    result.keyword_ = "test1";
    result.category_ = Category::Region;
    result.loc = KeywordLocation::serializeObject();
    result.type_ = Type::Pressure;
    result.name_ = "test2";
    result.number_ = 2;
    result.userDefined_ = true;

    return result;
}

SummaryConfigNode& SummaryConfigNode::fip_region(const std::string& fip_region)
{
    this->fip_region_ = fip_region;
    return *this;
}


SummaryConfigNode& SummaryConfigNode::parameterType(const Type type)
{
    this->type_ = type;
    return *this;
}

SummaryConfigNode& SummaryConfigNode::namedEntity(std::string name)
{
    this->name_ = std::move(name);
    return *this;
}

SummaryConfigNode& SummaryConfigNode::number(const int num)
{
    this->number_ = num;
    return *this;
}

SummaryConfigNode& SummaryConfigNode::isUserDefined(const bool userDefined)
{
    this->userDefined_ = userDefined;
    return *this;
}

std::string SummaryConfigNode::uniqueNodeKey() const
{
    switch (this->category()) {
    case SummaryConfigNode::Category::Well: [[fallthrough]];
    case SummaryConfigNode::Category::Node: [[fallthrough]];
    case SummaryConfigNode::Category::Group:
        return this->keyword() + ':' + this->namedEntity();

    case SummaryConfigNode::Category::Field: [[fallthrough]];
    case SummaryConfigNode::Category::Miscellaneous:
        return this->keyword();

    case SummaryConfigNode::Category::Aquifer: [[fallthrough]];
    case SummaryConfigNode::Category::Region: [[fallthrough]];
    case SummaryConfigNode::Category::Block:
        return this->keyword() + ':' + std::to_string(this->number());

    case SummaryConfigNode::Category::Connection: [[fallthrough]];
    case SummaryConfigNode::Category::Segment:
        return this->keyword() + ':' + this->namedEntity() + ':' + std::to_string(this->number());
    }

    throw std::invalid_argument {
        "Unhandled Summary Parameter Category '"
        + to_string(this->category()) + '\''
    };
}

bool operator==(const SummaryConfigNode& lhs, const SummaryConfigNode& rhs)
{
    if (lhs.keyword() != rhs.keyword()) return false;

    assert (lhs.category() == rhs.category());

    switch( lhs.category() ) {
        case SummaryConfigNode::Category::Field: [[fallthrough]];
        case SummaryConfigNode::Category::Miscellaneous:
            // Fully identified by keyword
            return true;

        case SummaryConfigNode::Category::Well: [[fallthrough]];
        case SummaryConfigNode::Category::Node: [[fallthrough]];
        case SummaryConfigNode::Category::Group:
            // Equal if associated to same named entity
            return lhs.namedEntity() == rhs.namedEntity();

        case SummaryConfigNode::Category::Aquifer: [[fallthrough]];
        case SummaryConfigNode::Category::Region: [[fallthrough]];
        case SummaryConfigNode::Category::Block:
            // Equal if associated to same numeric entity
            return lhs.number() == rhs.number();

        case SummaryConfigNode::Category::Connection: [[fallthrough]];
        case SummaryConfigNode::Category::Segment:
            // Equal if associated to same numeric
            // sub-entity of same named entity
            return (lhs.namedEntity() == rhs.namedEntity())
                && (lhs.number()      == rhs.number());
    }

    return false;
}

bool operator<(const SummaryConfigNode& lhs, const SummaryConfigNode& rhs)
{
    if (lhs.keyword() < rhs.keyword()) return true;
    if (rhs.keyword() < lhs.keyword()) return false;

    // If we get here, the keyword are equal.

    switch( lhs.category() ) {
        case SummaryConfigNode::Category::Field: [[fallthrough]];
        case SummaryConfigNode::Category::Miscellaneous:
            // Fully identified by keyword.
            // Return false for equal keywords.
            return false;

        case SummaryConfigNode::Category::Well: [[fallthrough]];
        case SummaryConfigNode::Category::Node: [[fallthrough]];
        case SummaryConfigNode::Category::Group:
            // Ordering determined by namedEntityd entity
            return lhs.namedEntity() < rhs.namedEntity();

        case SummaryConfigNode::Category::Aquifer: [[fallthrough]];
        case SummaryConfigNode::Category::Region: [[fallthrough]];
        case SummaryConfigNode::Category::Block:
            // Ordering determined by numeric entity
            return lhs.number() < rhs.number();

        case SummaryConfigNode::Category::Connection: [[fallthrough]];
        case SummaryConfigNode::Category::Segment:
        {
            // Ordering determined by pair of named entity and numeric ID.
            //
            // Would ideally implement this in terms of operator< for
            // std::tuple<std::string,int>, with objects generated by std::tie(),
            // but `namedEntity()` does not return an lvalue.
            const auto& lnm = lhs.namedEntity();
            const auto& rnm = rhs.namedEntity();

            return ( lnm <  rnm)
                || ((lnm == rnm) && (lhs.number() < rhs.number()));
        }
    }

    throw std::invalid_argument {
        "Unhandled Summary Parameter Category '" + to_string(lhs.category()) + '\''
    };
}

// =====================================================================

SummaryConfig::SummaryConfig( const Deck& deck,
                              const Schedule& schedule,
                              const TableManager& tables,
                              const AquiferConfig& aquiferConfig,
                              const ParseContext& parseContext,
                              ErrorGuard& errors,
                              const GridDims& dims) {
    try {
        SUMMARYSection section( deck );

        const auto node_names = need_node_names(section) ? collect_node_names(schedule) : std::vector<std::string> {};

        for (const auto& kw : section) {
            if (is_processing_instruction(kw.name())) {
                handleProcessingInstruction(kw.name());
            } else {
                handleKW(this->m_keywords, node_names, kw, schedule, tables, aquiferConfig, parseContext, errors, dims);
            }
        }

        for (const auto& meta_pair : meta_keywords) {
            if (section.hasKeyword(meta_pair.first)) {
                const auto& deck_keyword = section.getKeyword(meta_pair.first);
                for (const auto& kw : meta_pair.second) {
                    if (!this->hasKeyword(kw)) {
                        KeywordLocation location = deck_keyword.location();
                        location.keyword = fmt::format("{}/{}", meta_pair.first, kw);

                        handleKW(this->m_keywords, kw, location, schedule, aquiferConfig, parseContext, errors);
                    }
                }
            }
        }

        uniq(this->m_keywords);
        for (const auto& kw : this->m_keywords) {
            this->short_keywords.insert(kw.keyword());
            this->summary_keywords.insert(kw.uniqueNodeKey());
        }
    }
    catch (const OpmInputError& opm_error) {
        throw;
    }
    catch (const std::exception& std_error) {
        OpmLog::error(fmt::format("An error occured while configuring the summary properties\n",
                                  "Internal error: {}", std_error.what()));
        throw;
    }
}


SummaryConfig::SummaryConfig( const Deck& deck,
                              const Schedule& schedule,
                              const TableManager& tables,
                              const AquiferConfig& aquiferConfig,
                              const ParseContext& parseContext,
                              ErrorGuard& errors) :
    SummaryConfig( deck , schedule, tables, aquiferConfig, parseContext, errors, GridDims( deck ))
{ }


template <typename T>
SummaryConfig::SummaryConfig( const Deck& deck,
                              const Schedule& schedule,
                              const TableManager& tables,
                              const AquiferConfig& aquiferConfig,
                              const ParseContext& parseContext,
                              T&& errors) :
        SummaryConfig(deck, schedule, tables, aquiferConfig, parseContext, errors)
{}


SummaryConfig::SummaryConfig( const Deck& deck,
               const Schedule& schedule,
               const TableManager& tables,
               const AquiferConfig& aquiferConfig) :
    SummaryConfig(deck, schedule, tables, aquiferConfig, ParseContext(), ErrorGuard())
{}


SummaryConfig::SummaryConfig(const keyword_list& kwds,
                             const std::set<std::string>& shortKwds,
                             const std::set<std::string>& smryKwds) :
    m_keywords(kwds), short_keywords(shortKwds), summary_keywords(smryKwds)
{}

SummaryConfig SummaryConfig::serializeObject()
{
    SummaryConfig result;
    result.m_keywords = {SummaryConfigNode::serializeObject()};
    result.short_keywords = {"test1"};
    result.summary_keywords = {"test2"};

    return result;
}

SummaryConfig::const_iterator SummaryConfig::begin() const {
    return this->m_keywords.cbegin();
}

SummaryConfig::const_iterator SummaryConfig::end() const {
    return this->m_keywords.cend();
}

SummaryConfig& SummaryConfig::merge( const SummaryConfig& other ) {
    this->m_keywords.insert( this->m_keywords.end(),
                             other.m_keywords.begin(),
                             other.m_keywords.end() );

    uniq( this->m_keywords );
    return *this;
}

SummaryConfig& SummaryConfig::merge( SummaryConfig&& other ) {
    auto fst = std::make_move_iterator( other.m_keywords.begin() );
    auto lst = std::make_move_iterator( other.m_keywords.end() );
    this->m_keywords.insert( this->m_keywords.end(), fst, lst );
    other.m_keywords.clear();

    uniq( this->m_keywords );
    return *this;
}


bool SummaryConfig::hasKeyword( const std::string& keyword ) const {
    return short_keywords.find(keyword) != short_keywords.end();
}


bool SummaryConfig::hasSummaryKey(const std::string& keyword ) const {
    return summary_keywords.find(keyword) != summary_keywords.end();
}

const SummaryConfigNode& SummaryConfig::operator[](std::size_t index) const {
    return this->m_keywords[index];
}


bool SummaryConfig::match(const std::string& keywordPattern) const {
    int flags = 0;
    for (const auto& keyword : this->short_keywords) {
        if (fnmatch(keywordPattern.c_str(), keyword.c_str(), flags) == 0)
            return true;
    }
    return false;
}

SummaryConfig::keyword_list SummaryConfig::keywords(const std::string& keywordPattern) const {
    keyword_list kw_list;
    int flags = 0;
    for (const auto& keyword : this->m_keywords) {
        if (fnmatch(keywordPattern.c_str(), keyword.keyword().c_str(), flags) == 0)
            kw_list.push_back(keyword);
    }
    return kw_list;
}


size_t SummaryConfig::size() const {
    return this->m_keywords.size();
}

/*
  Can be used to query if a certain 3D field, e.g. PRESSURE, is
  required to calculate the summary variables.

  The implementation is based on the hardcoded datastructure
  required_fields defined in a anonymous namespaces at the top of this
  file; the content of this datastructure again is based on the
  implementation of the Summary calculations in the opm-output
  repository: opm/output/eclipse/Summary.cpp.
*/

bool SummaryConfig::require3DField( const std::string& keyword ) const {
    const auto iter = required_fields.find( keyword );
    if (iter == required_fields.end())
        return false;

    for (const auto& kw : iter->second) {
        if (this->hasKeyword( kw ))
            return true;
    }

    return false;
}



std::set<std::string> SummaryConfig::fip_regions() const {
    std::set<std::string> reg_set;
    for (const auto& node : this->m_keywords) {
        if (node.category() == EclIO::SummaryNode::Category::Region)
            reg_set.insert( node.fip_region() );
    }
    return reg_set;
}


bool SummaryConfig::operator==(const Opm::SummaryConfig& data) const {
    return this->m_keywords == data.m_keywords &&
           this->short_keywords == data.short_keywords &&
           this->summary_keywords == data.summary_keywords;
}

void SummaryConfig::handleProcessingInstruction(const std::string& keyword) {
    if (keyword == "RUNSUM") {
        runSummaryConfig.create = true;
    } else if (keyword == "NARROW") {
        runSummaryConfig.narrow = true;
    } else if (keyword == "SEPARATE") {
        runSummaryConfig.separate = true;
    }
}

}
