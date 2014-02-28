#include <math.h>
#include "meraxes.h"
#include "tree_flags.h"

static void inline assign_galaxy_to_halo(galaxy_t *gal, halo_t *halo)
{
  if (halo->Galaxy == NULL)
    halo->Galaxy = gal;
  else {
    SID_log_error("Trying to assign first galaxy to a halo which already has a first galaxy!");
#ifdef DEBUG
    mpi_debug_here();
#endif
    ABORT(EXIT_FAILURE);
  }

}

static void inline create_new_galaxy(
  run_globals_t *run_globals,
  int            snapshot,
  halo_t        *halo,
  int           *NGal,
  int           *new_gal_counter,
  int           *unique_ID)
{
  galaxy_t *gal;

  gal = new_galaxy(run_globals, unique_ID);
  gal->Halo = halo;
  gal->LTTime = run_globals->LTTime[snapshot];
  assign_galaxy_to_halo(gal, halo);

  if (run_globals->LastGal != NULL)
    run_globals->LastGal->Next = gal;
  else
    run_globals->FirstGal = gal;

  run_globals->LastGal = gal;
  gal->FirstGalInHalo = gal;
  gal->dt = run_globals->LTTime[0] - gal->LTTime;
  gal->dM = halo->Mvir;
  // gal->dM = 0.;
  *NGal = *NGal+1;
  *new_gal_counter = *new_gal_counter+1;
}

static void inline turn_off_merger_flag(galaxy_t *gal)
{
  gal->TreeFlags = gal->TreeFlags & (~TREE_CASE_MERGER);
}

static void inline kill_galaxy(
  run_globals_t *run_globals,
  galaxy_t      *gal,
  galaxy_t      *prev_gal,
  int                *NGal,
  int                *kill_counter)
{
  galaxy_t *cur_gal;

  // Remove it from the global linked list
  if(prev_gal!=NULL)
    prev_gal->Next = gal->Next;
  else
    run_globals->FirstGal = gal->Next;

  // If it is a type 2 then also remove it from the linked list of galaxies in its halo
  cur_gal = gal->FirstGalInHalo;
  if (cur_gal != gal)
  {
    while ((cur_gal->NextGalInHalo != gal) && (cur_gal->NextGalInHalo != NULL))
      cur_gal = cur_gal->NextGalInHalo;
    cur_gal->NextGalInHalo = gal->NextGalInHalo;
  }

  // Finally deallocated the galaxy and decrement any necessary counters
  SID_free(SID_FARG gal);
  *NGal = *NGal-1;
  *kill_counter = *kill_counter+1;
}

static inline bool check_for_flag(int flag, int tree_flags)
{
  if ((tree_flags & flag)==flag)
    return true;
  else
    return false;
}

static inline bool check_for_merger(int flags)
{
 if ((flags & TREE_CASE_MERGER)==TREE_CASE_MERGER)
   return true;
 else
   return false;
}

static inline bool check_if_valid_host(run_globals_t *run_globals, halo_t *halo)
{
  int invalid_flags = (TREE_CASE_FRAGMENTED_RETURNED
      | TREE_CASE_STRAYED
      | TREE_CASE_SPUTTERED);

  if((halo->Type == 0)
      && (halo->Galaxy == NULL)
      && (halo->TreeFlags & invalid_flags)==0)
    return true;
  else
    return false;

  // SID_log("halo ID=%d is not a valid host (Type=%d, Treeflags=%d)", SID_LOG_COMMENT, halo->ID, halo->Type, halo->TreeFlags);
}

static int find_original_index(int index, int *lookup, int n_mappings)
{
  int *pointer = NULL;
  int new_index = -1;

  pointer = bsearch(&index, lookup, (size_t)n_mappings, sizeof(int), compare_ints);
  if(pointer)
    new_index = (int)(pointer-lookup);

  return new_index;
}

//! Actually run the model
void dracarys(run_globals_t *run_globals)
{

  trees_info_t  trees_info;
  trees_info_t *snapshot_trees_info = NULL;
  halo_t      **snapshot_halo   = NULL;
  halo_t       *halo            = NULL;
  fof_group_t **snapshot_fof_group       = NULL;
  fof_group_t  *fof_group       = NULL;
  galaxy_t     *gal             = NULL;
  galaxy_t     *prev_gal        = NULL;
  galaxy_t     *next_gal        = NULL;
  galaxy_t     *cur_gal         = NULL;
  int         **snapshot_index_lookup    = NULL;
  int          *index_lookup    = NULL;
  int           i_newhalo;
  int           NGal            = 0;
  int           unique_ID       = 0;
  int           nout_gals;
  int           last_nout_gals;
  int           last_snap       = 0;
  int           kill_counter    = 0;
  int           merger_counter  = 0;
  int           new_gal_counter = 0;
  int           ghost_counter   = 0;
  int           n_store_snapshots = 0;
  bool          flag_multiple_runs = (bool)(run_globals->params.MultipleRuns_Flag);
  int           n_runs          = run_globals->params.NMultipleRuns;
  int           i_snap;

  // Find what the last requested output snapshot is
  for(int ii=0; ii<NOUT; ii++)
    if (run_globals->ListOutputSnaps[ii] > last_snap)
      last_snap = run_globals->ListOutputSnaps[ii];

  // Allocate an array of last_snap halo array pointers
  if(flag_multiple_runs)
    n_store_snapshots = last_snap+1;
  else
    n_store_snapshots = 1;
  snapshot_halo = (halo_t **)SID_calloc(sizeof(halo_t *) * n_store_snapshots);
  snapshot_fof_group = (fof_group_t **)SID_calloc(sizeof(fof_group_t *) * n_store_snapshots);
  snapshot_index_lookup = (int **)SID_calloc(sizeof(int *) * n_store_snapshots);
  snapshot_trees_info = (trees_info_t *)SID_calloc(sizeof(trees_info_t) * n_store_snapshots);
  for(int ii=0; ii < n_store_snapshots; ii++)
    snapshot_trees_info[ii].n_halos = -1;

  // Loop through each model iteration
  for(int i_run=0; i_run<n_runs; i_run++)
  {

    SID_log("Starting model iteration %d...", SID_LOG_OPEN|SID_LOG_TIMER, i_run);
    // Loop through each snapshot
    for(int snapshot=0; snapshot<=last_snap; snapshot++)
    {

      // Reset book keeping counters
      kill_counter    = 0;
      merger_counter  = 0;
      new_gal_counter = 0;
      ghost_counter   = 0;

      // Read in the halos for this snapshot
      if(flag_multiple_runs)
        i_snap = snapshot;
      else
        i_snap = 0;
      trees_info = read_halos(run_globals, snapshot, &(snapshot_halo[i_snap]), &(snapshot_fof_group[i_snap]), &(snapshot_index_lookup[i_snap]), snapshot_trees_info);

      // Set the relevant pointers to this snapshot
      halo = snapshot_halo[i_snap];
      fof_group = snapshot_fof_group[i_snap];
      index_lookup = snapshot_index_lookup[i_snap];

      SID_log("Processing snapshot %d (z=%.2f)...", SID_LOG_OPEN|SID_LOG_TIMER, snapshot, run_globals->ZZ[snapshot]);

#ifdef USE_TOCF
      // Calculate the critical halo mass for cooling
      if((run_globals->params.TOCF_Flag) && (tocf_params.uvb_feedback))
        calculate_Mvir_crit(run_globals, run_globals->ZZ[snapshot]);
#endif

      // Reset the halo pointers and ghost flags for all galaxies and decrement
      // the snapskip counter
      gal      = run_globals->FirstGal;
      while(gal!=NULL)
      {
        gal->Halo = NULL;
        gal->ghost_flag = false;
        gal->SnapSkipCounter--;
        gal = gal->Next;
      }

      gal      = run_globals->FirstGal;
      prev_gal = NULL;
      while (gal != NULL) {

        i_newhalo = gal->HaloDescIndex;

        if(gal->SnapSkipCounter<=0)
        {

          if((index_lookup) && (i_newhalo > -1) && !(gal->ghost_flag) && (gal->Type < 2))
            i_newhalo = find_original_index(gal->HaloDescIndex, index_lookup, trees_info.n_halos);

          if(i_newhalo>-1)
          {
            gal->OldType = gal->Type;
            gal->dt      = gal->LTTime - run_globals->LTTime[snapshot];
            if(gal->Type < 2)
            {

              if(check_for_merger(gal->TreeFlags))
              {

                // Here we have a new merger...  Mark it and deal with it below.
                gal->Type = 999;
                merger_counter++;
                gal->Halo = &(halo[i_newhalo]);
                turn_off_merger_flag(gal);

              } else
              {

                // Here we have the simplest case where a galaxy continues along in it's halo...
                gal->dM   = (halo[i_newhalo]).Mvir - gal->Mvir;

                gal->Halo = &(halo[i_newhalo]);
                assign_galaxy_to_halo(gal, &(halo[i_newhalo]));

                // Loop through all of the other galaxies in this halo and set their Halo pointer
                cur_gal = gal->NextGalInHalo;
                while(cur_gal!=NULL)
                {
                  cur_gal->Halo = &(halo[i_newhalo]);
                  cur_gal       = cur_gal->NextGalInHalo;
                }

              }
            }
          } else  // this galaxy has been marked for death
          {

            if(gal->FirstGalInHalo==gal)
            {
              // We have marked the first galaxy in the halo for death. If there are any
              // other type 2 galaxies in this halo then we must kill them as well...
              // Unfortunately, we don't know if we have alrady processed these
              // remaining galaxies, so we have to just mark them for the moment
              // and then do another check below...
              cur_gal = gal->NextGalInHalo;
              while(cur_gal!=NULL)
              {
                cur_gal->HaloDescIndex = -1;
                cur_gal = cur_gal->NextGalInHalo;
              }
            }
            kill_galaxy(run_globals, gal, prev_gal, &NGal, &kill_counter);
            gal = prev_gal;

          }
        } else  // this galaxy's halo has skipped this snapshot
        {

          // This is a ghost galaxy for this snapshot.
          // We need to count all the other galaxies in this halo as ghosts as
          // well since they won't be reachable by traversing the FOF groups
          cur_gal = gal;
          while (cur_gal!=NULL)
          {
            if(cur_gal->HaloDescIndex > -1)
            {
              ghost_counter++;
              cur_gal->ghost_flag = true;
            }
            cur_gal = cur_gal->NextGalInHalo;
          }

        }

        // gal may be NULL if we just killed the first galaxy
        if (gal!=NULL)
        {
          prev_gal = gal;
          gal      = gal->Next;
        } else
          gal = run_globals->FirstGal;

      }

      // Do one more pass to make sure that we have killed all galaxies which we
      // should have (i.e. satellites in strayed halos etc.)
      prev_gal = NULL;
      gal = run_globals->FirstGal;
      while(gal!=NULL)
      {
        if(gal->HaloDescIndex < 0)
        {
          kill_galaxy(run_globals, gal, prev_gal, &NGal, &kill_counter);
          gal = prev_gal;
        }
        prev_gal = gal;
        gal      = gal->Next;
      }

      // Store the number of ghost galaxies present at this snapshot
      run_globals->NGhosts = ghost_counter;

      // Incase we ended up removing the last galaxy, update the LastGal pointer
      run_globals->LastGal = prev_gal;

      // Find empty (valid) type 0 halos and place new galaxies in them
      for(int i_halo=0; i_halo<trees_info.n_halos; i_halo++)
        if(check_if_valid_host(run_globals, &(halo[i_halo])))
          create_new_galaxy(run_globals, snapshot, &(halo[i_halo]), &NGal, &new_gal_counter, &unique_ID);

      // Loop through each galaxy and deal with HALO mergers now that all other
      // galaxies have been processed and their halo pointers updated...
      gal = run_globals->FirstGal;
      while (gal != NULL)
      {
        if(gal->Type == 999)
        {
          if(gal->Halo->Galaxy == NULL)
          {

            // Here we have a halo with a galaxy that has just merged into an
            // empty halo.  From the point of view of the model, this isn't
            // actually a merger and so we need to catch these cases...
            gal->dM           = gal->Halo->Mvir - gal->Mvir;
            gal->Halo->Galaxy = gal;
            gal->Type         = gal->Halo->Type;
            cur_gal           = gal->NextGalInHalo;
            while(cur_gal!=NULL)
            {
              cur_gal->Halo = gal->Halo;
              cur_gal       = cur_gal->NextGalInHalo;
            }

          } else  // there are galaxies in the halo being merged into
          {

            // Remember that we have already set the galaxy's halo pointer so we
            // don't need to do that here.

            gal->Type = 2;

            // Add the incoming galaxy to the end of the halo's linked list
            cur_gal = gal->Halo->Galaxy;
            while (cur_gal!=NULL) {
              prev_gal = cur_gal;
              cur_gal  = cur_gal->NextGalInHalo;
            }
            prev_gal->NextGalInHalo = gal;

            // Update the FirstGalInHalo pointer.
            gal->FirstGalInHalo = gal->Halo->Galaxy;

            // Loop through and update the FirstGalInHalo and Halo pointers of any other
            // galaxies that are attached to the incoming galaxy
            cur_gal = gal->NextGalInHalo;
            while(cur_gal!=NULL)
            {
              cur_gal->FirstGalInHalo = gal->FirstGalInHalo;
              cur_gal->Halo           = gal->Halo;
              cur_gal                 = cur_gal->NextGalInHalo;
            }

            if (gal->FirstGalInHalo == NULL)
              SID_log_warning("Just set gal->FirstGalInHalo = NULL!", SID_LOG_COMMENT);

            // Set the merger target of the incoming galaxy and initialise the
            // merger clock.  Note that we *increment* the clock imemdiately
            // after calculating it. This is because we will decrement the clock
            // (by the same amount) when checking for mergers in evolve.c
            gal->MergerTarget = gal->FirstGalInHalo;
            gal->MergTime     = calculate_merging_time(run_globals, gal, snapshot);
            gal->MergTime    += gal->dt;
          }
        }

        gal = gal->Next;
      }

      // We finish by copying the halo properties into the galaxy structure of
      // all galaxies with type<2 and updating the lookback time values for
      // non-ghosts.
      gal = run_globals->FirstGal;
      while(gal!=NULL)
      {
        if((gal->Halo==NULL) && (!gal->ghost_flag))
        {
          SID_log_error("We missed a galaxy during processing!");
#ifdef DEBUG
          mpi_debug_here();
#endif
          ABORT(EXIT_FAILURE);
        }
        if(!gal->ghost_flag)
          gal->LTTime = run_globals->LTTime[snapshot];
        if((gal->Type<2) && (!gal->ghost_flag))
          copy_halo_to_galaxy(gal->Halo, gal, snapshot);
        gal = gal->Next;
      }

#ifdef DEBUG
      if(NGal>0)
        check_counts(run_globals, fof_group, NGal, trees_info.n_fof_groups);
#endif

      // Do the physics
      if(NGal > 0)
        nout_gals = evolve_galaxies(run_globals, fof_group, snapshot, NGal, trees_info.n_fof_groups);

      // Add the ghost galaxies into the nout_gals count
      nout_gals+=ghost_counter;

#ifdef USE_TOCF
      if(run_globals->params.TOCF_Flag)
      {
        if(!tocf_params.uvb_feedback)
        {
          // We are decoupled, so no need to run 21cmFAST unless we are ouputing this snapshot
          for(int i_out = 0; i_out < NOUT; i_out++)
            if(snapshot == run_globals->ListOutputSnaps[i_out])
              call_find_HII_bubbles(run_globals, snapshot, nout_gals);
        }
        else
          call_find_HII_bubbles(run_globals, snapshot, nout_gals);
      }
#endif

#ifdef DEBUG
      // print some statistics for this snapshot
      SID_Allreduce(SID_IN_PLACE, &merger_counter , 1, SID_INT, SID_SUM, SID.COMM_WORLD);
      SID_Allreduce(SID_IN_PLACE, &kill_counter   , 1, SID_INT, SID_SUM, SID.COMM_WORLD);
      SID_Allreduce(SID_IN_PLACE, &new_gal_counter, 1, SID_INT, SID_SUM, SID.COMM_WORLD);
      SID_Allreduce(SID_IN_PLACE, &ghost_counter  , 1, SID_INT, SID_SUM, SID.COMM_WORLD);

      SID_log("Newly identified merger events    :: %d", SID_LOG_COMMENT, merger_counter);
      SID_log("Killed galaxies                   :: %d", SID_LOG_COMMENT, kill_counter);
      SID_log("Newly created galaxies            :: %d", SID_LOG_COMMENT, new_gal_counter);
      SID_log("Galaxies in ghost halos           :: %d", SID_LOG_COMMENT, ghost_counter);
#endif

      // Write the results if this is a requested snapshot (and we are on our last model call)
      if(i_run == n_runs-1)
      {
        for(int i_out = 0; i_out < NOUT; i_out++)
          if(snapshot == run_globals->ListOutputSnaps[i_out])
            write_snapshot(run_globals, nout_gals, i_out, &last_nout_gals);
      }

#ifdef USE_TOCF
      if(run_globals->params.TOCF_Flag)
        check_if_reionization_complete(run_globals);
#endif

      SID_log("...done", SID_LOG_CLOSE);

    }

    // Tidy up counters and galaxies from this iteration
    NGal            = 0;
    unique_ID       = 0;

    SID_log("Resetting halo->galaxy pointers", SID_LOG_COMMENT);
    for(int ii=0; ii < n_store_snapshots; ii++)
    {
      for(int jj=0; jj < snapshot_trees_info[ii].n_halos; jj++)
        snapshot_halo[ii][jj].Galaxy = NULL;
    }

    SID_log("Freeing galaxies...", SID_LOG_OPEN);
    gal = run_globals->FirstGal;
    while (gal != NULL) {
      next_gal = gal->Next;
      SID_free(SID_FARG gal);
      gal = next_gal;
    }
    run_globals->FirstGal = NULL;
    SID_log(" ...done", SID_LOG_CLOSE);

    SID_log("... finished iteration %d", SID_LOG_CLOSE, i_run);

  }

  // Create the master file
  if(SID.My_rank == 0)
    create_master_file(run_globals);

  // Free all of the remaining allocated galaxies, halos and fof groups
  SID_log("Freeing FOF groups and halos...", SID_LOG_COMMENT);
  for(int ii=0; ii<n_store_snapshots; ii++)
  {
    SID_free(SID_FARG snapshot_halo[ii]);
    SID_free(SID_FARG snapshot_fof_group[ii]);
    SID_free(SID_FARG snapshot_index_lookup[ii]);
  }
  SID_free(SID_FARG snapshot_halo);
  SID_free(SID_FARG snapshot_fof_group);
  SID_free(SID_FARG snapshot_index_lookup);
  SID_free(SID_FARG snapshot_trees_info);

}


