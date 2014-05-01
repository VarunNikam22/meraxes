#define _MAIN
#include "meraxes.h"
#include <sys/stat.h>
#include <hdf5.h>

#ifdef USE_TOCF
#include <21cmfast.h>
#endif

static void cleanup(run_globals_t *run_globals)
{
  SID_log("Running cleanup...", SID_LOG_OPEN);

  cleanup_mags(run_globals);

  if (run_globals->RequestedForestId)
    SID_free(SID_FARG run_globals->RequestedForestId);

#ifdef USE_TOCF
  if (run_globals->params.TOCF_Flag)
    free_reionization_grids(run_globals);
#endif

  SID_log("Freeing hdf5 related stuff...", SID_LOG_OPEN);
  SID_free(SID_FARG run_globals->hdf5props.field_types);
  SID_free(SID_FARG run_globals->hdf5props.field_names);
  SID_free(SID_FARG run_globals->hdf5props.dst_field_sizes);
  SID_free(SID_FARG run_globals->hdf5props.dst_offsets);
  H5Tclose(run_globals->hdf5props.array3f_tid);
  SID_log(" ...done", SID_LOG_CLOSE);

  gsl_rng_free(run_globals->random_generator);

  SID_free(SID_FARG run_globals->LTTime);
  SID_free(SID_FARG run_globals->ZZ);
  SID_free(SID_FARG run_globals->AA);

  SID_log(" ...done", SID_LOG_CLOSE);

#ifdef DEBUG
  // close the debug file
  fclose(meraxes_debug_file);
#endif

  // close the log file
  // if(SID.n_proc > 1)
  // {
  //   fflush(SID.fp_log);
  //   fclose(SID.fp_log);
  // }
}

void myexit(int signum)
{
  fprintf(stderr, "Task: %d\tnode: %s\tis exiting.\n\n\n", SID.My_rank, SID.My_node);
  cn_quote();
  // if(SID.n_proc > 1)
  // {
  //   fflush(SID.fp_log);
  //   fclose(SID.fp_log);
  // }
  SID_exit(signum);
}

/*
   static void set_physics_params(
   run_globals_t *run_globals,
   double             *vals,
   int                 n_params)
   {
   physics_params_t *phys_par = &(run_globals->params.physics);

   if ((n_params==3) || (n_params==6)){
    phys_par->peak            = vals[0];
    phys_par->sigma           = vals[1];
    phys_par->stellarfrac     = vals[2];
    if (n_params==6){
      phys_par->peak_evo        = vals[3];
      phys_par->sigma_evo       = vals[4];
      phys_par->stellarfrac_evo = vals[5];
    }
    if (SID.My_rank==0){
      printf("Changed physics_peak to %g\n"        , phys_par->peak);
      printf("Changed physics_sigma to %g\n"       , phys_par->sigma);
      printf("Changed physics_stellarfrac to %g\n" , phys_par->stellarfrac);
      if (n_params==6){
        printf("Changed physics_peak_evo to %g\n"        , phys_par->peak_evo);
        printf("Changed physics_sigma_evo to %g\n"       , phys_par->sigma_evo);
        printf("Changed physics_stellarfrac_evo to %g\n" , phys_par->stellarfrac_evo);
      }
    }
   }
   }
 */


int main(int argc, char **argv)
{
  // init SID
  SID_init(&argc, &argv, NULL);

  struct stat filestatus;
  run_globals_t run_globals;

  // char log_fname[50];
  // FILE *log_file = NULL;
  // if(SID.n_proc > 1)
  // {
  //   SID.flag_log_allranks = 1;
  //   sprintf(log_fname, "rank_%d.log", SID.My_rank);
  //   log_file = fopen(log_fname, "w");
  //   SID.fp_log = log_file;
  // }

  // deal with any input arguments
  if ((argc != 8) && (argc != 4) && (argc != 2))
  {
    if (SID.My_rank == 0)
    {
      printf("\n  usage: %s <parameterfile>\n\n", argv[0]);
      ABORT(1);
    }
  }

#ifdef DEBUG
  // open the debug file for this core
  char debug_fname[50];
  sprintf(debug_fname, "debug_%d.txt", SID.My_rank);
  meraxes_debug_file = fopen(debug_fname, "w");
  // if(SID.My_rank==0)
  //   mpi_debug_here();
#endif

#ifdef USE_TOCF
  // Note that this must be done *before* we read the parameter file as we may
  // want to overwrite some of the set defaults.
  init_default_tocf_params();
#endif

  // read the input parameter file
  read_parameter_file(&run_globals, argv[1]);

  // Check to see if the output directory exists and if not, create it
  if (stat(run_globals.params.OutputDir, &filestatus) != 0)
    mkdir(run_globals.params.OutputDir, 02755);

  // Deal with any command line parameter values
  // if (argc==8){
  //   double *physics_param_vals = SID_malloc(sizeof(double) * (argc-2));
  //   for(int i=0; i<argc-2; i++)
  //     physics_param_vals[i] = atof(argv[i+2]);
  //   set_physics_params(&run_globals, physics_param_vals, argc-2);
  //   SID_free(SID_FARG physics_param_vals);
  // }

  // initiate meraxes
  init_meraxes(&run_globals);

  // calculate the output hdf5 file properties for later use
  calc_hdf5_props(&run_globals);

  // Run the model!
  dracarys(&run_globals);

  // cleanup
  cleanup(&run_globals);

  SID_exit(EXIT_SUCCESS);
}
