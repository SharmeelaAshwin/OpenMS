// --------------------------------------------------------------------------
//                   OpenMS -- Open-Source Mass Spectrometry
// --------------------------------------------------------------------------
// Copyright The OpenMS Team -- Eberhard Karls University Tuebingen,
// ETH Zurich, and Freie Universitaet Berlin 2002-2016.
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
// $Maintainer: Timo Sachsenberg $
// $Authors: Andreas Bertsch $
// --------------------------------------------------------------------------

#include <OpenMS/ANALYSIS/TARGETED/TargetedExperimentHelper.h>
#include <OpenMS/ANALYSIS/TARGETED/TargetedExperiment.h>
#include <OpenMS/CHEMISTRY/ModificationsDB.h>

namespace OpenMS
{
  namespace TargetedExperimentHelper
  {

    void setModification(int location, int max_size, String modification, OpenMS::AASequence& aas)
    {
      OPENMS_PRECONDITION(location >= -1 && location <= max_size, 
          (String("Location has invalid value") + (String)location).c_str() )

      if (location == -1)
      {
        aas.setNTerminalModification(modification);
      }
      else if (location == max_size)
      {
        aas.setCTerminalModification(modification);
      }
      else
      {
        aas.setModification(location, modification);
      }
    }

    OpenMS::AASequence getAASequence(const OpenMS::TargetedExperiment::Peptide& peptide)
    {
      OpenMS::ModificationsDB* mod_db = OpenMS::ModificationsDB::getInstance();
      OpenMS::AASequence aas = AASequence::fromString(peptide.sequence);

      for (std::vector<Peptide::Modification>::const_iterator it = peptide.mods.begin(); 
          it != peptide.mods.end(); ++it)
      {
        // Step 1: First look whether the UniMod ID is set (we dont use a CVTerm any more but a member)
        if (it->unimod_id != -1)
        {
          setModification(it->location, boost::numeric_cast<int>(peptide.sequence.size()), 
              "UniMod:" + String(it->unimod_id), aas);
          continue;
        }
        // compare with code in source/ANALYSIS/OPENSWATH/DATAACCESS/DataAccessHelper.cpp

        // Step 2: If the above step fails, try to find the correct
        // modification by using the mass difference
        const ResidueModification* mod = mod_db->getBestModificationByDiffMonoMass(
          it->mono_mass_delta, 1.0, peptide.sequence[it->location]);
        if (mod != NULL)
        {
          setModification(it->location, boost::numeric_cast<int>(peptide.sequence.size()), mod->getId(), aas);
        }
        else
        {
          // could not find any modification ...
          std::cerr << "Warning: Could not determine modification with delta mass " <<
            it->mono_mass_delta << " for peptide " << peptide.sequence <<
            " at position " << it->location << std::endl;
          std::cerr << "Skipping this modifcation" << std::endl;
        }
      }
      return aas;
    }

  }
}
