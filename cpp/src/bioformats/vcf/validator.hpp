#ifndef OPENCB_VCF_VALIDATOR_HPP
#define OPENCB_VCF_VALIDATOR_HPP

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <boost/algorithm/string.hpp>

#include "file_structure.hpp"

namespace opencb
{
  namespace vcf
  {
    
    struct ParsingState
    {
        size_t n_lines;
        size_t n_columns;
        size_t n_batches;

        int cs;
        bool m_is_valid;
        
        std::shared_ptr<Source> source;
        std::shared_ptr<std::vector<Record>> records;
     
        ParsingState(std::shared_ptr<Source> source,
                     std::shared_ptr<std::vector<Record>> records);
        
        void set_version(std::string & fileformat) const;
        
        void add_meta(MetaEntry const & meta) const;
        
        void add_record(Record const & record) const;
        
        std::vector<std::string> const & samples() const;
        
        void set_samples(std::vector<std::string> & samples) const;
    };

    class ParsingError : public std::runtime_error
    {
        using runtime_error::runtime_error;
    };

    class IgnoreParsePolicy
    {
      public:
        void handle_token_begin(ParsingState const & state) {}
        void handle_token_char(ParsingState const & state, char c) {}
        void handle_token_end(ParsingState const & state) {}
        void handle_newline(ParsingState const & state) {}
        
        void handle_fileformat(ParsingState const & state) {}
        
        void handle_meta_typeid(ParsingState const & state) {}
        void handle_meta_typeid(ParsingState const & state, std::string type_id) {}
        void handle_meta_key(ParsingState const & state) {}
        void handle_meta_key(ParsingState const & state, std::string key) {}
        void handle_meta_value(ParsingState const & state) {}
        void handle_meta_line(ParsingState const & state) {}
        
        void handle_column_end(ParsingState const & state, size_t n_columns);
        void handle_body_line(ParsingState & state) {}
        
        std::string current_token() const { return ""; }
    };

    class StoreParsePolicy
    {
      public:
        void handle_token_begin(ParsingState const & state)
        {
            m_current_token = std::string();
        }

        void handle_token_char(ParsingState const & state, char c)
        {
            m_current_token.push_back(c);
        }
        
        void handle_token_end(ParsingState const & state) 
        {
            m_grouped_tokens.push_back(m_current_token);
        }
        
        void handle_newline(ParsingState const & state) 
        {
            m_current_token.clear();
            m_grouped_tokens.clear();
            m_line_tokens.clear();
        }
        
        
        void handle_fileformat(ParsingState const & state)
        {
            state.set_version(m_current_token);
        }
        
        
        void handle_meta_typeid(ParsingState const & state)
        {
            m_line_typeid = m_current_token;
        }
        
        void handle_meta_typeid(ParsingState const & state, std::string type_id) 
        {
            m_line_typeid = type_id;
        }
        
        void handle_meta_key(ParsingState const & state) 
        {
            m_grouped_tokens.push_back(m_current_token);
        }
        
        void handle_meta_key(ParsingState const & state, std::string key)
        {
            m_grouped_tokens.push_back(key);
        }
        
        void handle_meta_value(ParsingState const & state) 
        {
            m_grouped_tokens.push_back(m_current_token);
        }
        
        void handle_meta_line(ParsingState const & state) 
        {
            // Put together m_line_typeid and m_grouped_tokens in a single MetaEntry object
            // Add MetaEntry to Source
            try {
                if (m_line_typeid == "") { // Plain value
                    state.add_meta(MetaEntry{m_grouped_tokens[0], state.source});

                } else if (m_grouped_tokens.size() == 1) { // TypeID=value
                    state.add_meta(MetaEntry{m_line_typeid, m_grouped_tokens[0], state.source});

                } else if (m_grouped_tokens.size() % 2 == 0) { // TypeID=<Key-value pairs>
                    auto key_values = std::map<std::string, std::string>{};
                    for (size_t i = 0; i < m_grouped_tokens.size(); i += 2) {
                        key_values[m_grouped_tokens[i]] = m_grouped_tokens[i+1];
                    }
                    state.add_meta(MetaEntry{m_line_typeid, key_values, state.source});

                } else {
                    // TODO Throw exception
                }
            } catch (std::invalid_argument ex) {
                throw ParsingError(ex.what());
            }
        }
        
        
        void handle_sample_name(ParsingState const & state)
        {
            m_grouped_tokens.push_back(m_current_token);
        }
        
        void handle_header_line(ParsingState const & state) 
        {
            state.set_samples(m_grouped_tokens);
        }
        
        
        void handle_column_end(ParsingState const & state, size_t n_columns) 
        {
            switch(n_columns) {
                case 1:
                    m_line_tokens["CHROM"] = m_grouped_tokens;
                    break;
                case 2:
                    m_line_tokens["POS"] = m_grouped_tokens;
                    break;
                case 3:
                    m_line_tokens["ID"] = m_grouped_tokens;
                    break;
                case 4:
                    m_line_tokens["REF"] = m_grouped_tokens;
                    break;
                case 5:
                    m_line_tokens["ALT"] = m_grouped_tokens;
                    break;
                case 6:
                    m_line_tokens["QUAL"] = m_grouped_tokens;
                    break;
                case 7:
                    m_line_tokens["FILTER"] = m_grouped_tokens;
                    break;
                case 8:
                    m_line_tokens["INFO"] = m_grouped_tokens;
                    break;
                case 9:
                    m_line_tokens["FORMAT"] = m_grouped_tokens;
                    break;
                default:
                    // Collection of samples
                    if (m_line_tokens.find("SAMPLES") == m_line_tokens.end()) {
                        m_line_tokens["SAMPLES"] = std::vector<std::string>{};
                    }
                    // Samples are stored as a single string
                    m_line_tokens["SAMPLES"].push_back(m_grouped_tokens[0]);
            }
            m_grouped_tokens = std::vector<std::string>{};
        }
        
        void handle_body_line(ParsingState const & state) 
        {
            try {
                // Transform the position token into a size_t
                auto position = static_cast<size_t>(std::stoi(m_line_tokens["POS"][0]));
                
                // Transform all the quality tokens into floating point numbers
                float quality;
                try {
                    quality = std::stof(m_line_tokens["QUAL"][0]);
                } catch (std::invalid_argument ex) {
                    quality = 0;
                }
                
                // Split the info tokens by the equals (=) symbol
                std::map<std::string, std::string> info;
                for (auto & field : m_line_tokens["INFO"]) {
                    std::vector<std::string> subfields;
                    boost::split(subfields, field, boost::is_any_of("="));
                    if (subfields.size() > 1) {
                        info.emplace(subfields[0], subfields[1]);
                    } else {
                        info.emplace(subfields[0], "");
                    }
                }
                
                state.add_record(Record {
                    m_line_tokens["CHROM"][0],
                    position,
                    m_line_tokens["ID"],
                    m_line_tokens["REF"][0],
                    m_line_tokens["ALT"],
                    quality,
                    m_line_tokens["FILTER"],
                    info,
                    m_line_tokens["FORMAT"],
                    m_line_tokens["SAMPLES"],
                    state.source
                });
            } catch (std::invalid_argument ex) {
                throw ParsingError(ex.what());
            }
        }
        
        
        std::string current_token() const
        {
            return m_current_token;
        }

      private:
        /**
         * Token being currently parsed
         */
        std::string m_current_token;
        
        /**
         * Token that acts as type ID for the whole line, like ALT/FILTER in meta entries
         */
        std::string m_line_typeid;
        
        /**
         * Tokens that must be grouped, like all key-value pairs in the INFO column
         */
        std::vector<std::string> m_grouped_tokens;
        
        /**
         * Tokens read in a line and grouped by an ID
         */
        std::map<std::string, std::vector<std::string>> m_line_tokens;
    };

    class AbortErrorPolicy
    {
      public:
        void handle_fileformat_section_error(ParsingState const & state, 
                std::string message = "Error in file format section");
        
        void handle_meta_section_error(ParsingState const & state, 
                std::string message = "Error in meta-data section");
        
        void handle_header_section_error(ParsingState const & state, 
                std::string message = "Error in header section");
        
        void handle_body_section_error(ParsingState const & state, 
                std::string message = "Error in body section");
    };

    class ReportErrorPolicy
    {
      public:
        void handle_fileformat_section_error(ParsingState const & state, 
                std::string message = "Error in file format section");
        
        void handle_meta_section_error(ParsingState const & state, 
                std::string message = "Error in meta-data section");
        
        void handle_header_section_error(ParsingState const & state, 
                std::string message = "Error in header section");
        
        void handle_body_section_error(ParsingState const & state, 
                std::string message = "Error in body section");
    };

    // Only check syntax
    struct QuickValidatorCfg
    {
      using ParsePolicy = IgnoreParsePolicy;
      using ErrorPolicy = ReportErrorPolicy;
    };

    // Check both syntax and semantics
    struct FullValidatorCfg
    {
      using ParsePolicy = StoreParsePolicy;
      using ErrorPolicy = ReportErrorPolicy;
    };

    // Read the file for processing, assuming it is correct
    struct ReaderCfg
    {
      using ParsePolicy = StoreParsePolicy;
      using ErrorPolicy = AbortErrorPolicy;
    };

    template <typename Configuration>
    class Parser
    : ParsingState,
      Configuration::ParsePolicy,
      Configuration::ErrorPolicy
    {
      public:
        using ParsePolicy = typename Configuration::ParsePolicy;
        using ErrorPolicy = typename Configuration::ErrorPolicy;

        Parser(std::shared_ptr<Source> const & source,
               std::shared_ptr<std::vector<Record>> const & records);

        void parse(std::string const & text);
        void parse(std::vector<char> const & text);

        void end();

        bool is_valid() const;
        
      private:
        void parse_buffer(char const * p, char const * pe, char const * eof);
        
    };

    // Predefined aliases for common uses of the parser
    using QuickValidator = Parser<QuickValidatorCfg>;
    using FullValidator = Parser<FullValidatorCfg>;
    using Reader = Parser<ReaderCfg>;
  }
}

#include "validator_detail.hpp"

#endif
