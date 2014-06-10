// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2013.
//
// This software is released under a three-clause BSD license:
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of any author or any participating institution
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// For a full list of authors, refer to the file AUTHORS.
// --------------------------------------------------------------------------
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL ANY OF THE AUTHORS OR THE CONTRIBUTING
// INSTITUTIONS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
// WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
// OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
// ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// --------------------------------------------------------------------------
// $Maintainer: Chris Bielow $
// $Authors: Andreas Bertsch, Chris Bielow, Knut Reinert $
// --------------------------------------------------------------------------

#include <OpenMS/APPLICATIONS/TOPPBase.h>
#include <OpenMS/CHEMISTRY/EnzymaticDigestion.h>
#include <OpenMS/DATASTRUCTURES/SeqanIncludeWrapper.h>
#include <OpenMS/FORMAT/IdXMLFile.h>
#include <OpenMS/FORMAT/FASTAFile.h>
#include <OpenMS/METADATA/ProteinIdentification.h>
#include <OpenMS/SYSTEM/File.h>

#include <algorithm>

using namespace OpenMS;
using namespace std;

//-------------------------------------------------------------
//Doxygen docu
//-------------------------------------------------------------

/**
    @page TOPP_PeptideIndexer PeptideIndexer

    @brief Refreshes the protein references for all peptide hits from a idXML file.

<CENTER>
    <table>
        <tr>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. predecessor tools </td>
            <td VALIGN="middle" ROWSPAN=2> \f$ \longrightarrow \f$ PeptideIndexer \f$ \longrightarrow \f$</td>
            <td ALIGN = "center" BGCOLOR="#EBEBEB"> pot. successor tools </td>
        </tr>
        <tr>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> @ref TOPP_IDFilter or @n any protein/peptide processing tool </td>
            <td VALIGN="middle" ALIGN = "center" ROWSPAN=1> @ref TOPP_FalseDiscoveryRate </td>
        </tr>
    </table>
</CENTER>

  Each peptide hit is annotated by a target_decoy string,
  indicating if the peptide sequence is found in a 'target', a 'decoy' or in both 'target+decoy' protein. This information is
  crucial for the @ref TOPP_FalseDiscoveryRate @ref TOPP_IDPosteriorErrorProbability tools.

  @note Make sure that your protein names in the database contain a correctly formatted decoy string. This can be ensured by using @ref UTILS_DecoyDatabase.
        If the decoy identifier is not recognized successfully all proteins will be assumed to stem from the target-part of the query.<br>
        E.g., "sw|P33354_REV|YEHR_ECOLI Uncharacterized lipop..." is <b>invalid</b>, since the tool has no knowledge of how SwissProt entries are build up.
        A correct identifier could be "rev_sw|P33354|YEHR_ECOLI Uncharacterized li ..." or "sw|P33354|YEHR_ECOLI_rev Uncharacterized li", depending on if you are
        using prefix annotation or not.<br>
        This tool will also give you some target/decoy statistics when its done. Look carefully!


  This tool supports relative database filenames, which (when not found in the current working directory) is looked up in
  the directories specified by 'OpenMS.ini:id_db_dir' (see @subpage TOPP_advanced).

  By default the tool will fail, if an unmatched peptide occurs, i.e. the database does not contain the corresponding protein.
  You can force the tool to return successfully in this case by using the flag 'allow_unmatched'.

  Some search engines (such as Mascot) will replace ambiguous AA's ('B', 'Z', and 'X') in the protein database with unambiguous AA' in the reported peptides, e.g., exchange 'X' with 'H'.
  This will cause this peptide not to be found by exactly matching its sequence to the database. However, we can recover these cases by using tolerant search (done automatically).

  Two search modes are available:
    - exact: Peptide sequences require exact match in protein database.
             If at least one protein hit is found, no tolerant search is used for this peptide.
             If no protein for this peptide can be found, tolerant matching is automatically used for this peptide.
    - tolerant:
             Allow ambiguous AA's in protein sequence, e.g., 'M' in peptide will match 'X' in protein.
             This mode might yield more protein hits for some peptides (those that contain ambiguous AAs).

  No matter if exact or tolerant search is used, we require ambiguous AA's in peptide sequence to match exactly in the protein DB (i.e., 'X' in peptide only matches 'X' in database).
  The exact mode is much faster (about x10) and consumes less memory (about x2.5), but might fail to report a few protein hits with ambiguous AAs for some peptides. Usually these proteins are putative, however.
  The exact mode also supports usage of multiple threads (use @ -threads option) to speed up computation even further, at the cost of some memory. This is only for the exact search though (Aho Corasick). If tolerant searching
  needs to be done for unassigned peptides, the latter will consume the major time portion.

  Once a peptide sequence is found in a protein sequence, this does <b>not</b> imply that the hit is valid! This is where enzyme specificity comes into play.
  By default, we demand that the peptide is fully tryptic (since the enzyme parameter is set to "trypsin" and specificity is "full").
  So unless the peptide coincides with C- and/or N-terminus of the protein, the peptide's cleavage pattern should fulfill the trypsin cleavage rule [KR][^P].
  We make one exception for peptides which start at the second AA of the protein where the first AA of the protein is methionin (M), which is usually cleaved off in vivo, e.g.,
  the two peptides AAAR and MAAAR would both match a protein starting with MAAAR.

  You can relax the requirements further by chosing <tt>semi-tryptic</tt> (only one of two "internal" termini must match requirements) or <tt>none</tt> (essentially allowing all hits, no matter their context).


  <B>The command line parameters of this tool are:</B>
  @verbinclude TOPP_PeptideIndexer.cli
  <B>INI file documentation of this tool:</B>
  @htmlinclude TOPP_PeptideIndexer.html
*/


namespace seqan
{

  struct FoundProteinFunctor
  {
  public:
    typedef OpenMS::Map<OpenMS::Size, std::set<OpenMS::Size> > MapType;

    /// peptide index --> protein indices
    MapType pep_to_prot;

    /// number of accepted hits (passing addHit() constraints)
    OpenMS::Size filter_passed;

    /// number of rejected hits (not passing addHit())
    OpenMS::Size filter_rejected;

  private:
    EnzymaticDigestion enzyme_;

  public:
    FoundProteinFunctor(const EnzymaticDigestion& enzyme) :
      pep_to_prot(),
      filter_passed(0),
      filter_rejected(0),
      enzyme_(enzyme)
    {
    }

    template <typename TIter1, typename TIter2>
    void operator()(const TIter1& iter_pep, const TIter2& iter_prot)
    {
      // the peptide sequence (will not change)
      const OpenMS::String tmp_pep(begin(representative(iter_pep)), end(representative(iter_pep)));

      // remember mapping of proteins to peptides and vice versa
      const OpenMS::Size count_occ = countOccurrences(iter_pep);
      for (OpenMS::Size i_pep = 0; i_pep < count_occ; ++i_pep)
      {
        const OpenMS::Size idx_pep = getOccurrences(iter_pep)[i_pep].i1;
        const OpenMS::Size count_occ_prot = countOccurrences(iter_prot);
        for (OpenMS::Size i_prot = 0; i_prot < count_occ_prot; ++i_prot)
        {
          const seqan::Pair<int> prot_occ = getOccurrences(iter_prot)[i_prot];
          // the protein sequence (will change for every Occurrence -- hitting multiple proteins)
          const OpenMS::String tmp_prot(begin(indexText(container(iter_prot))[getSeqNo(prot_occ)]), end(indexText(container(iter_prot))[getSeqNo(prot_occ)]));
          // check if hit is valid and add (if valid)
          addHit(idx_pep, prot_occ.i1, tmp_pep, tmp_prot, getSeqOffset(prot_occ) );
        }
      }
    }

    void addHit(OpenMS::Size idx_pep, OpenMS::Size idx_prot, const OpenMS::String& seq_pep, const OpenMS::String& protein, OpenMS::Size position)
    {
      if (enzyme_.isValidProduct(AASequence::fromString(protein), position, seq_pep.length()))
      {
        pep_to_prot[idx_pep].insert(idx_prot);
        ++filter_passed;
      }
      else
      {
        //LOG_WARN << "Peptide " << seq_pep << " is not a valid hit to protein " << protein << " @ " << position << std::endl;
        ++filter_rejected;
      }
    }

    bool operator==(const FoundProteinFunctor& rhs) const
    {
      if (pep_to_prot.size() != rhs.pep_to_prot.size())
      {
        LOG_ERROR << "Size " << pep_to_prot.size() << " " << rhs.pep_to_prot.size() << std::endl;
        return false;
      }

      MapType::const_iterator it1 = pep_to_prot.begin(), it2 = rhs.pep_to_prot.begin();
      while (it1 != pep_to_prot.end())
      {
        if (it1->first != it2->first)
        {
          LOG_ERROR << "Index of " << it1->first << " " << it2->first << std::endl;
          return false;
        }
        if (it1->second.size() != it2->second.size())
        {
          LOG_ERROR << "Size of " << it1->first << " " << it1->second.size() << "--" << it2->second.size() << std::endl;
          return false;
        }
        if (!equal(it1->second.begin(), it1->second.end(), it2->second.begin()))
        {
          LOG_ERROR << "not equal set for " << it1->first << std::endl;
          return false;
        }
        ++it1;
        ++it2;
      }
      return true;
    }

    bool operator!=(const FoundProteinFunctor& rhs) const
    {
      return !(*this == rhs);
    }

  };


  // saving some memory for the SA
  template <>
  struct SAValue<Index<StringSet<Peptide>, IndexWotd<> > >
  {
    typedef Pair<unsigned> Type;
  };

  template <typename T = void>
  struct EquivalenceClassAA_
  {
    static unsigned const VALUE[24];
  };
  template <typename T>
  unsigned const EquivalenceClassAA_<T>::VALUE[24] =
  {
    1, // 0 Ala Alanine
    2, // 1 Arg Arginine
    4, // 2 Asn Asparagine
    8, // 3 Asp Aspartic Acid
    16, // 4 Cys Cystine
    32, // 5 Gln Glutamine
    64, // 6 Glu Glutamic Acid
    128, // 7 Gly Glycine
    256, // 8 His Histidine
    512, // 9 Ile Isoleucine
    1024, //10 Leu Leucine
    2048, //11 Lys Lysine
    4096, //12 Met Methionine
    8192, //13 Phe Phenylalanine
    16384, //14 Pro Proline
    32768, //15 Ser Serine
    65536, //16 Thr Threonine
    131072, //17 Trp Tryptophan
    262144, //18 Tyr Tyrosine
    524288, //19 Val Valine
    12, //20 Aspartic Acid, Asparagine
    96, //21 Glutamic Acid, Glutamine
    static_cast<unsigned>(-1), //22 Unknown (matches ALL)
    static_cast<unsigned>(-1), //23 Terminator (dummy)
  };


  template <
    bool enumerateA,
    bool enumerateB,
    typename TOnFoundFunctor,
    typename TTreeIteratorA,
    typename TIterPosA,
    typename TTreeIteratorB,
    typename TIterPosB,
    typename TErrors>
  inline void
  _approximateAminoAcidTreeSearch(
    TOnFoundFunctor& onFoundFunctor,
    TTreeIteratorA    iterA,
    TIterPosA     iterPosA,
    TTreeIteratorB    iterB_,
    TIterPosB     iterPosB,
    TErrors           errorsLeft,
    TErrors     classErrorsLeft)
  {
    if (enumerateA && !goDown(iterA))
      return;

    if (enumerateB && !goDown(iterB_))
      return;

    do
    {
      TTreeIteratorB iterB = iterB_;
      do
      {
        TErrors e = errorsLeft;
        TErrors ec = classErrorsLeft;
        TIterPosA ipA = iterPosA;
        TIterPosB ipB = iterPosB;

        while (true)
        {
          if (ipA == repLength(iterA))
          {
            if (isLeaf(iterA))
            {
              onFoundFunctor(iterA, iterB);
              break;
            }

            if (ipB == repLength(iterB) && !isLeaf(iterB))
              _approximateAminoAcidTreeSearch<true, true>(onFoundFunctor, iterA, ipA, iterB, ipB, e, ec);
            else
              _approximateAminoAcidTreeSearch<true, false>(onFoundFunctor, iterA, ipA, iterB, ipB, e, ec);
            break;
          }
          else
          {
            if (ipB == repLength(iterB))
            {
              if (!isLeaf(iterB))
                _approximateAminoAcidTreeSearch<false, true>(onFoundFunctor, iterA, ipA, iterB, ipB, e, ec);
              break;
            }
          }

          if (_charComparator(representative(iterA)[ipA],
                              representative(iterB)[ipB],
                              EquivalenceClassAA_<char>::VALUE))
          {
            // matched (including character classes) - look at ambiguous AA in PROTEIN tree (peptide tree is not considered!)
            const char x_prot = representative(iterB)[ipB];
            if (x_prot == 'X' ||
                x_prot == 'B' ||
                x_prot == 'Z')
            {
              if (ec == 0)
                break;
              --ec;
            }

            // dealing with 'X' in peptide sequence: only match exactly 'X' in proteinDB, not just any representative of 'X' (this is how X!Tandem would report results)
            const char x_pep = representative(iterA)[ipA];
            if (x_pep == 'X' ||
                x_pep == 'B' ||
                x_pep == 'Z')
            {
              if (x_pep != x_prot)
                break;
            }

          }
          else
          {
            if (e == 0)
              break;
            --e;
          }


          ++ipA;
          ++ipB;
        }
      }
      while (enumerateB && goRight(iterB));
    }
    while (enumerateA && goRight(iterA));
  }

  template <
    typename TEquivalenceTable>
  inline bool
  _charComparator(
    AminoAcid charA,
    AminoAcid charB,
    TEquivalenceTable equivalence)
  {
    const unsigned a_index = ordValue(charA);
    const unsigned b_index = ordValue(charB);
    return (equivalence[a_index] & equivalence[b_index]) != 0;
  }

}


// We do not want this class to show up in the docu:
/// @cond TOPPCLASSES

class TOPPPeptideIndexer :
  public TOPPBase
{
public:
  TOPPPeptideIndexer() :
    TOPPBase("PeptideIndexer", "Refreshes the protein references for all peptide hits.")
  {
  }

protected:
  void registerOptionsAndFlags_()
  {
    registerInputFile_("in", "<file>", "", "Input idXML file containing the identifications.");
    setValidFormats_("in", ListUtils::create<String>("idXML"));
    registerInputFile_("fasta", "<file>", "", "Input sequence database in FASTA format. Non-existing relative file-names are looked up via'OpenMS.ini:id_db_dir'", true, false, ListUtils::create<String>("skipexists"));
    setValidFormats_("fasta", ListUtils::create<String>("fasta"));
    registerOutputFile_("out", "<file>", "", "Output idXML file.");
    setValidFormats_("out", ListUtils::create<String>("idXML"));
    registerStringOption_("decoy_string", "<string>", "_rev", "String that was appended (or prepended - see 'prefix' flag below) to the accession of the protein database to indicate a decoy protein.", false);
    registerStringOption_("missing_decoy_action", "<action>", "error", "Action to take if NO peptide was assigned to a decoy protein (which indicates wrong database or decoy string): 'error' (exit with error, no output), 'warn' (exit with success, warning message)", false);
    setValidStrings_("missing_decoy_action", ListUtils::create<String>("error,warn"));

    registerTOPPSubsection_("enzyme", "The enzyme determines valid cleavage-sites and the cleavage specificity set by the user determines how these are enforced.");

    registerStringOption_("enzyme:name", "", EnzymaticDigestion::NamesOfEnzymes[0], "Enzyme which determines valid cleavage sites, e.g., for trypsin it should (unless at protein terminus) end on K or R and the AA-before should also be K or R, and not followed by proline.", false);
    StringList enzymes;
    enzymes.assign(EnzymaticDigestion::NamesOfEnzymes, EnzymaticDigestion::NamesOfEnzymes + EnzymaticDigestion::SIZE_OF_ENZYMES);
    setValidStrings_("enzyme:name", enzymes);

    registerStringOption_("enzyme:specificity", "", EnzymaticDigestion::NamesOfSpecificity[0], "Specificity of the enzyme."
                                                                            "\n  '" + EnzymaticDigestion::NamesOfSpecificity[0] + "': both internal cleavage-sites must match."
                                                                            "\n  '" + EnzymaticDigestion::NamesOfSpecificity[1] + "': one of two internal cleavage-sites must match."
                                                                            "\n  '" + EnzymaticDigestion::NamesOfSpecificity[2] + "': allow all peptide hits no matter their context. Therefore, the enzyme chosen does not play a role here", false);
    StringList spec;
    spec.assign(EnzymaticDigestion::NamesOfSpecificity, EnzymaticDigestion::NamesOfSpecificity + EnzymaticDigestion::SIZE_OF_SPECIFICITY);
    setValidStrings_("enzyme:specificity", spec);

    registerFlag_("write_protein_sequence", "If set, the protein sequences are stored as well.");
    registerFlag_("prefix", "If set, the database has protein accessions with 'decoy_string' as prefix.");
    registerFlag_("keep_unreferenced_proteins", "If set, protein hits which are not referenced by any peptide are kept.");
    registerFlag_("allow_unmatched", "If set, unmatched peptide sequences are allowed. By default (i.e. this flag is not set) the program terminates with error status on unmatched peptides.");
    registerFlag_("full_tolerant_search", "If set, all peptide sequences are matched using tolerant search. Thus potentially more proteins (containing ambiguous AA's) are associated. This is much slower!");
    registerIntOption_("aaa_max", "<AA count>", 4, "Maximal number of ambiguous amino acids (AAA) allowed when matching to a protein DB with AAA's. AAA's are 'B', 'Z', and 'X'", false);
    setMinInt_("aaa_max", 0);
  }

  ExitCodes main_(int, const char**)
  {
    //-------------------------------------------------------------
    // parsing parameters
    //-------------------------------------------------------------
    String in(getStringOption_("in"));
    String out(getStringOption_("out"));
    bool write_protein_sequence(getFlag_("write_protein_sequence"));
    bool prefix(getFlag_("prefix"));
    bool keep_unreferenced_proteins(getFlag_("keep_unreferenced_proteins"));
    bool allow_unmatched(getFlag_("allow_unmatched"));

    String decoy_string(getStringOption_("decoy_string"));

    String db_name(getStringOption_("fasta"));
    if (!File::readable(db_name))
    {
      String full_db_name;
      try
      {
        full_db_name = File::findDatabase(db_name);
      }
      catch (...)
      {
        printUsage_();
        return ILLEGAL_PARAMETERS;
      }
      db_name = full_db_name;
    }

    EnzymaticDigestion enzyme;
    enzyme.setEnzyme(enzyme.getEnzymeByName(getStringOption_("enzyme:name")));
    enzyme.setSpecificity(enzyme.getSpecificityByName(getStringOption_("enzyme:specificity")));


    //-------------------------------------------------------------
    // reading input
    //-------------------------------------------------------------

    // we stream the Fasta file
    vector<FASTAFile::FASTAEntry> proteins;
    FASTAFile().load(db_name, proteins);

    vector<ProteinIdentification> prot_ids;
    vector<PeptideIdentification> pep_ids;
    IdXMLFile().load(in, prot_ids, pep_ids);

    //-------------------------------------------------------------
    // calculations
    //-------------------------------------------------------------

    if (proteins.size() == 0) // we do not allow an empty database
    {
      LOG_ERROR << "Error: An empty FASTA file was provided. Mapping makes no sense. Aborting..." << std::endl;
      return INPUT_FILE_EMPTY;
    }


    if (pep_ids.size() == 0) // Aho-Corasick requires non-empty input
    {
      LOG_WARN << "Warning: An empty idXML file was provided. Output will be empty as well." << std::endl;
      if (!getFlag_("keep_unreferenced_proteins"))
      {
        prot_ids.clear();
      }
      IdXMLFile().store(out, prot_ids, pep_ids);
      return EXECUTION_OK;
    }

    writeDebug_("Collecting peptides...", 1);

    seqan::FoundProteinFunctor func(enzyme); // stores the matches (need to survive local scope which follows)
    Map<String, Size> acc_to_prot; // build map: accessions to proteins

    { // new scope - forget data after search

      /**
       BUILD Protein DB
      */

      seqan::StringSet<seqan::Peptide> prot_DB;

      for (Size i = 0; i != proteins.size(); ++i)
      {
        // build Prot DB
        seqan::appendValue(prot_DB, proteins[i].sequence.substitute("*","").c_str());

        // consistency check
        String acc = proteins[i].identifier;
        if (acc_to_prot.has(acc))
        {
          writeLog_(String("PeptideIndexer: error, identifiers of proteins should be unique to a database, identifier '") + acc + String("' found multipe times."));
        }
        acc_to_prot[acc] = i;
      }

      /**
        BUILD Peptide DB
      */
      seqan::StringSet<seqan::Peptide> pep_DB;
      for (vector<PeptideIdentification>::const_iterator it1 = pep_ids.begin(); it1 != pep_ids.end(); ++it1)
      {
        //String run_id = it1->getIdentifier();
        vector<PeptideHit> hits = it1->getHits();
        for (vector<PeptideHit>::iterator it2 = hits.begin(); it2 != hits.end(); ++it2)
        {
          appendValue(pep_DB, it2->getSequence().toUnmodifiedString().substitute("*","").c_str());
        }
      }

      writeLog_(String("Mapping ") + length(pep_DB) + " peptides to " + length(prot_DB) + " proteins.");

      /** first, try Aho Corasick (fast) -- using exact matching only */
      bool SA_only = getFlag_("full_tolerant_search");
      if (!SA_only)
      {
        StopWatch sw;
        sw.start();
        SignedSize protDB_length = (SignedSize) length(prot_DB);
#ifdef _OPENMP
#pragma omp parallel
#endif
        {
          seqan::Pattern<seqan::StringSet<seqan::Peptide>, seqan::AhoCorasick> pattern(pep_DB);
          seqan::FoundProteinFunctor func_threads(enzyme);
          writeDebug_("Finding peptide/protein matches...", 1);

#pragma omp for
          for (SignedSize i = 0; i < protDB_length; ++i)
          {
            seqan::Finder<seqan::Peptide> finder(prot_DB[i]);
            while (find(finder, pattern))
            {
              //seqan::appendValue(pat_hits, seqan::Pair<Size, Size>(position(pattern), position(finder)));

              //func_threads.pep_to_prot[position(pattern)].insert(i);
              // String(seqan::String<char, seqan::CStyle>(prot_DB[i])), position(finder))
              // target.assign(begin(source, Standard()), end(source, Standard()));
              const seqan::Peptide& tmp_pep = pep_DB[position(pattern)];
              const seqan::Peptide& tmp_prot = prot_DB[i];

              func_threads.addHit(position(pattern), i, String(begin(tmp_pep), end(tmp_pep)), String(begin(tmp_prot), end(tmp_prot)), position(finder));
            }
          }

          // join results again
#ifdef _OPENMP
#pragma omp critical(PeptideIndexer_joinAC)
#endif
          {
            func.filter_passed += func_threads.filter_passed;
            func.filter_rejected += func_threads.filter_rejected;
            for (seqan::FoundProteinFunctor::MapType::const_iterator it = func_threads.pep_to_prot.begin(); it != func_threads.pep_to_prot.end(); ++it)
            {
              func.pep_to_prot[it->first].insert(func_threads.pep_to_prot[it->first].begin(), func_threads.pep_to_prot[it->first].end());
            }

          }
        } // end parallel

        sw.stop();

        writeLog_(String("Aho-Corasick done. Found ") + func.filter_passed + " hits in " + func.pep_to_prot.size() + " of " + length(pep_DB) + " peptides (time: " + sw.getClockTime() + " (wall) " + sw.getCPUTime() + " (CPU)).");
      }

      /// check if every peptide was found:
      if (func.pep_to_prot.size() != length(pep_DB))
      {
        /** search using SA, which supports mismatches (introduced by resolving ambiguous AA's by, e.g. Mascot) -- expensive! */
        writeLog_(String("Using SA to find ambiguous matches ..."));

        // search peptides which remained unidentified during Aho-Corasick (might be all if 'full_tolerant_search' is enabled)
        seqan::StringSet<seqan::Peptide> pep_DB_SA;
        Map<Size, Size> missed_pep;
        for (Size p = 0; p < length(pep_DB); ++p)
        {
          if (!func.pep_to_prot.has(p))
          {
            missed_pep[length(pep_DB_SA)] = p;
            appendValue(pep_DB_SA, pep_DB[p]);
          }
        }

        writeLog_(String("    for ") + length(pep_DB_SA) + " peptides.");

        seqan::FoundProteinFunctor func_SA(enzyme);

        typedef seqan::Index<seqan::StringSet<seqan::Peptide>, seqan::IndexWotd<> > TIndex;
        TIndex prot_Index(prot_DB);
        TIndex pep_Index(pep_DB_SA);

        // use only full peptides in Suffix Array
        const Size length_SA = length(pep_DB_SA);
        resize(indexSA(pep_Index), length_SA);
        for (Size i = 0; i < length_SA; ++i)
        {
          indexSA(pep_Index)[i].i1 = (unsigned)i;
          indexSA(pep_Index)[i].i2 = 0;
        }

        typedef seqan::Iterator<TIndex, seqan::TopDown<seqan::PreorderEmptyEdges> >::Type TTreeIter;

        //seqan::open(indexSA(prot_Index), "c:\\tmp\\prot_Index.sa");
        //seqan::open(indexDir(prot_Index), "c:\\tmp\\prot_Index.dir");

        TTreeIter prot_Iter(prot_Index);
        TTreeIter pep_Iter(pep_Index);

        UInt max_aaa = getIntOption_("aaa_max");
        seqan::_approximateAminoAcidTreeSearch<true, true>(func_SA, pep_Iter, 0u, prot_Iter, 0u, 0u, max_aaa);

        //seqan::save(indexSA(prot_Index), "c:\\tmp\\prot_Index.sa");
        //seqan::save(indexDir(prot_Index), "c:\\tmp\\prot_Index.dir");

        // augment results with SA hits
        func.filter_passed += func_SA.filter_passed;
        func.filter_rejected += func_SA.filter_rejected;
        for (seqan::FoundProteinFunctor::MapType::const_iterator it = func_SA.pep_to_prot.begin(); it != func_SA.pep_to_prot.end(); ++it)
        {
          func.pep_to_prot[missed_pep[it->first]] = it->second;
        }


      }

    } // end local scope

    // write some stats
    LOG_INFO << "Peptide hits which passed enzyme filter: " << func.filter_passed << "\n"
             << "                   rejected  by  filter: " << func.filter_rejected << std::endl;

    /* do mapping */

    writeDebug_("Reindexing peptide/protein matches...", 1);


    /// index existing proteins
    Map<String, Size> runid_to_runidx; // identifier to index
    for (Size run_idx = 0; run_idx < prot_ids.size(); ++run_idx)
    {
      runid_to_runidx[prot_ids[run_idx].getIdentifier()] = run_idx;
    }


    /// for peptides --> proteins
    Size stats_matched_unique(0);
    Size stats_matched_multi(0);
    Size stats_unmatched(0);
    Size stats_count_m_t(0);
    Size stats_count_m_d(0);
    Size stats_count_m_td(0);
    Map<Size, set<Size> > runidx_to_protidx; // in which protID do appear which proteins (according to mapped peptides)

    Size pep_idx(0);
    for (vector<PeptideIdentification>::iterator it1 = pep_ids.begin(); it1 != pep_ids.end(); ++it1)
    {
      vector<PeptideHit> hits = it1->getHits();

      // which ProteinIdentification does the peptide belong to?
      Size run_idx = runid_to_runidx[it1->getIdentifier()];

      for (vector<PeptideHit>::iterator it2 = hits.begin(); it2 != hits.end(); ++it2)
      {
        // clear protein accessions
        it2->setProteinAccessions(vector<String>());

        // add new protein references
        for (set<Size>::const_iterator it_i = func.pep_to_prot[pep_idx].begin();
             it_i != func.pep_to_prot[pep_idx].end();
             ++it_i)
        {
          it2->addProteinAccession(proteins[*it_i].identifier);

          runidx_to_protidx[run_idx].insert(*it_i); // fill protein hits

          /*
          /// STATS
          String acc = proteins[*it_i].identifier;
          // is the mapped protein in this run?
          if (accession_to_runidxs[acc].find(run_idx) ==
              accession_to_runidxs[acc].end())
          {
            ++stats_new_proteins; // this peptide was matched to a new protein
          }
          // remove proteins which we already saw (what remains is orphaned):
          runidx_to_accessions[run_idx].erase(acc);
          */
        }

        ///
        // add information whether this is a decoy hit
        ///
        bool matches_target(false);
        bool matches_decoy(false);

        for (vector<String>::const_iterator it = it2->getProteinAccessions().begin(); it != it2->getProteinAccessions().end(); ++it)
        {
          if (prefix)
          {
            if (it->hasPrefix(decoy_string)) matches_decoy = true;
            else matches_target = true;
          }
          else
          {
            if (it->hasSuffix(decoy_string)) matches_decoy = true;
            else matches_target = true;
          }
        }
        String target_decoy;
        if (matches_decoy && matches_target)
        {
          target_decoy = "target+decoy";
          ++stats_count_m_td;
        }
        else if (matches_target)
        {
          target_decoy = "target";
          ++stats_count_m_t;
        }
        else if (matches_decoy)
        {
          target_decoy = "decoy";
          ++stats_count_m_d;
        }
        it2->setMetaValue("target_decoy", target_decoy);
        if (it2->getProteinAccessions().size() == 1)
        {
          it2->setMetaValue("protein_references", "unique");
          ++stats_matched_unique;
        }
        else if (it2->getProteinAccessions().size() > 1)
        {
          it2->setMetaValue("protein_references", "non-unique");
          ++stats_matched_multi;
        }
        else
        {
          it2->setMetaValue("protein_references", "unmatched");
          ++stats_unmatched;
          if (stats_unmatched < 5) LOG_INFO << "  unmatched peptide: " << it2->getSequence() << "\n";
          else if (stats_unmatched == 5) LOG_INFO << "  unmatched peptide: ...\n";
        }

        ++pep_idx; // next hit
      }
      it1->setHits(hits);
    }

    LOG_INFO << "Statistics of peptides (target/decoy):\n";
    LOG_INFO << "  match to target DB only: " << stats_count_m_t << "\n";
    LOG_INFO << "  match to decoy DB only : " << stats_count_m_d << "\n";
    LOG_INFO << "  match to both          : " << stats_count_m_td << "\n";


    LOG_INFO << "Statistics of peptides (to protein mapping):\n";
    LOG_INFO << "  no match (to 0 protein)         : " << stats_unmatched << "\n";
    LOG_INFO << "  unique match (to 1 protein)     : " << stats_matched_unique << "\n";
    LOG_INFO << "  non-unique match (to >1 protein): " << stats_matched_multi << std::endl;


    /// exit if no peptides were matched to decoy
    if (stats_count_m_d + stats_count_m_td == 0)
    {
      String msg("No peptides were matched to the decoy portion of the database! Did you provide the correct concatenated database? Are your 'decoy_string' (=" + getStringOption_("decoy_string") + ") and 'prefix' (=" + String(getFlag_("prefix")) + ") settings correct?");
      if (getStringOption_("missing_decoy_action") == "error")
      {
        LOG_ERROR << "Error: " << msg << "\nSet 'missing_decoy_action' to 'warn' if you are sure this is ok!\nQuitting..." << std::endl;
        return UNEXPECTED_RESULT;
      }
      else
      {
        LOG_WARN << "Warn: " << msg << "\nSet 'missing_decoy_action' to 'error' if you want to elevate this to an error!" << std::endl;
      }


    }

    /// for proteins --> peptides

    Int stats_new_proteins(0);
    Int stats_orphaned_proteins(0);

    // all peptides contain the correct protein hit references, now update the protein hits
    for (Size run_idx = 0; run_idx < prot_ids.size(); ++run_idx)
    {
      set<Size> masterset = runidx_to_protidx[run_idx]; // all found protein matches

      vector<ProteinHit> new_protein_hits;
      // go through existing hits and update (do not create from anew, as there might be other information [score, rank] etc which
      //   we want to preserve
      for (vector<ProteinHit>::iterator p_hit = prot_ids[run_idx].getHits().begin(); p_hit != prot_ids[run_idx].getHits().end(); ++p_hit)
      {
        const String& acc = p_hit->getAccession();
        if (acc_to_prot.has(acc) // accession needs to exist in new FASTA file
           && masterset.find(acc_to_prot[acc]) != masterset.end())
        { // this accession was there already
          new_protein_hits.push_back(*p_hit);
          String seq;
          if (write_protein_sequence) seq = proteins[acc_to_prot[acc]].sequence;
          else seq = "";
          new_protein_hits.back().setSequence(seq);
          masterset.erase(acc_to_prot[acc]); // remove from master (at the end only new proteins remain)
        }
        else // old hit is orphaned
        {
          ++stats_orphaned_proteins;
          if (keep_unreferenced_proteins) new_protein_hits.push_back(*p_hit);
        }
      }

      // add remaining new hits
      for (set<Size>::const_iterator it = masterset.begin();
           it != masterset.end();
           ++it)
      {
        ProteinHit hit;
        hit.setAccession(proteins[*it].identifier);
        if (write_protein_sequence) hit.setSequence(proteins[*it].sequence);
        new_protein_hits.push_back(hit);
        ++stats_new_proteins;
      }


      prot_ids[run_idx].setHits(new_protein_hits);
    }

    LOG_INFO << "Statistics (proteins):\n";
    LOG_INFO << "  new proteins: " << stats_new_proteins << "\n";
    LOG_INFO << "  orphaned proteins: " << stats_orphaned_proteins << (keep_unreferenced_proteins ? " (all kept)" : " (all removed)") << "\n";

    writeDebug_("Ended reindexing", 1);

    //-------------------------------------------------------------
    // writing output
    //-------------------------------------------------------------

    IdXMLFile().store(out, prot_ids, pep_ids);

    if ((!allow_unmatched) && (stats_unmatched > 0))
    {
      LOG_WARN << "PeptideIndexer found unmatched peptides, which could not be associated to a protein.\n"
               << "Either:\n"
               << "   - check your FASTA database for completeness\n"
               << "   - set 'enzyme:specificity' to match the identification parameters of search engine\n"
               << "   - some engines (e.g. X!Tandem) employ loose cutting rules generating non-tryptic peptides\n"
               << "     If you trust them, disable enzyme specificity\n"
               << "   - increase 'aaa_max' to allow more ambiguous AA\n"
               << "   - as a last resort: use 'allow_unmatched' flag if unmatched peptides are ok\n"
               << "     Note that these peptides cannot be used for FDR or Quantification\n";

      LOG_WARN << "Result files were written, but program will return with error code" << std::endl;
      return UNEXPECTED_RESULT;
    }


    return EXECUTION_OK;
  }

};


int main(int argc, const char** argv)
{
  TOPPPeptideIndexer tool;
  return tool.main(argc, argv);
}

/// @endcond