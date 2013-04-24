#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gbpLib.h>
#include <gsl/gsl_rng.h>
#include <stdbool.h>
#include <hdf5.h>

/*
 * Definitions
 */

#define STRLEN  256  //!< Default string length
#define MAXTAGS 50   //!< Maximum number of allowed tags in input file
#define ALLOCFACTOR 2  //!< Size of galaxy array is ALLOCFACTOR*n_halos_max

// TODO: This should not be hard coded if at all possible...
#define MAXSNAPS 3  //!< Maximum number of snapshots

#ifndef NOUT
#define NOUT 1
#endif

#define MVIR_PROP 1
#define VMAX_PROP 2

#define ABORT(sigterm)                                                                 \
do {                                                                                   \
  SID_log_error("in file: %s\tfunc: %s\tline: %i", __FILE__, __FUNCTION__, __LINE__); \
  myexit(sigterm);                                                                     \
} while(0)

// Units (cgs):
#define GRAVITY          6.672e-8
#define SOLAR_MASS       1.989e33
#define SOLAR_LUM        3.826e33
#define RAD_CONST        7.565e-15
#define AVOGADRO         6.0222e23
#define BOLTZMANN        1.3806e-16
#define GAS_CONST        8.31425e7
#define C                2.9979e10
#define PLANCK           6.6262e-27
#define PROTONMASS       1.6726e-24
#define HUBBLE           3.2407789e-18 //! [h/sec]
#define SEC_PER_MEGAYEAR 3.155e13
#define SEC_PER_YEAR     3.155e7


/*
 * Structures
 */

//! Physics parameter values
struct physics_params_struct{
  int    funcprop;
  double peak;
  double sigma;
  double stellarfrac;
  double peak_evo;
  double sigma_evo;
  double stellarfrac_evo;
  double bhgrowthfactor;
};
typedef struct physics_params_struct physics_params_struct;

//! Run params
//! Everything in this structure is supplied by the user...
struct run_params_struct{
  char                  filename[STRLEN];
  char                  OutputDir[STRLEN];
  char                  FileNameGalaxies[STRLEN];
  char                  SimName[STRLEN];
  char                  SimulationDir[STRLEN];
  char                  CoolFunctionsDir[STRLEN];
  char                  FileWithOutputSnaps[STRLEN];
  int                   NEverySnap;
  int                   NScanSnap;
  int                   FilesPerSnapshot;
  int                   LastSnapShotNr;
  int                   FirstFile;
  int                   LastFile;
  double                BoxSize;
  double                VolumeFactor;
  double                ThreshMajorMerger;
  double                RecycleFraction;
  double                SimHubble_h;
  double                ObsHubble_h;
  int                   DiskInstabilityOn;
  double                BaryonFrac;
  double                Omega;
  double                OmegaLambda;
  double                PartMass;
  double                MergerTimeFactor;
  int                   SnaplistLength;
  physics_params_struct physics;
};
typedef struct run_params_struct run_params_struct;

struct run_units_struct{
  double UnitTime_in_s;
  double UnitLength_in_cm;
  double UnitVelocity_in_cm_per_s;
  double UnitTime_in_Megayears;
  double UnitMass_in_g;
  double UnitDensity_in_cgs;
  double UnitPressure_in_cgs;
  double UnitCoolingRate_in_cgs;
  double UnitEnergy_in_cgs;
};
typedef struct run_units_struct run_units_struct;

struct hdf5_output_struct
{
  size_t         dst_size;
  size_t        *dst_offsets;
  size_t        *dst_sizes;
  const char   **field_names;
  hid_t         *field_types;
  int            n_props;
};
typedef struct hdf5_output_struct hdf5_output_struct;

//! Global variables which will will be passed around
struct run_globals_struct{
  int                        LastOutputSnap;
  int                        ListOutputSnaps[NOUT];
  int                        Ngal;
  double                     AA[MAXSNAPS];
  double                     ZZ[MAXSNAPS];
  double                     Age[MAXSNAPS];
  double                     Hubble;
  double                     RhoCrit;
  double                     G;
  struct galaxy_struct      *FirstGal;
  struct galaxy_struct      *LastGal;
  gsl_rng                   *random_generator;
  run_params_struct          params;
  run_units_struct           units;
  hdf5_output_struct         hdf5props;
};
typedef struct run_globals_struct run_globals_struct;

//! The header from the input tree files.
struct trees_header_struct{
  int n_groups;
  int n_subgroups;
  int n_halos_max;
  int n_trees_subgroup;
  int n_trees_group;
};
typedef struct trees_header_struct trees_header_struct;

//! This is the structure for a halo in the catalog files
struct catalog_halo_struct{
  long long id_MBP;                    //!< ID of most bound particle in structure
  double    M_vir;                     //!< Bryan & Norman (ApJ 495, 80, 1998) virial mass [M_sol/h]
  int       n_particles;               //!< Number of particles in the structure
  float     position_COM[3];           //!< Centre-of-mass position      [Mpc/h]
  float     position_MBP[3];           //!< Most bound particle position [Mpc/h]
  float     velocity_COM[3];           //!< Centre-of-mass velocity      [km/s]
  float     velocity_MBP[3];           //!< Most bound particle velocity [km/s]
  float     R_vir;                     //!< Virial radius [Mpc/h]
  float     R_halo;                    //!< Distance of last halo particle from MBP [Mpc/h]
  float     R_max;                     //!< Radius of maximum circular velocity     [Mpc/h]
  float     V_max;                     //!< Maximum circular velocity               [km/s]
  float     sigma_v;                   //!< Total 3D velocity dispersion            [km/s]
  float     spin[3];                   //!< Specific angular momentum vector        [Mpc/h*km/s]
  float     q_triaxial;                //!< Triaxial shape parameter q=b/a
  float     s_triaxial;                //!< Triaxial shape parameter s=c/a
  float     shape_eigen_vectors[3][3]; //!< Normalized triaxial shape eigenvectors
  char      padding[8];                //!< Alignment padding
};
typedef struct catalog_halo_struct catalog_halo_struct;


//! The meraxis halo structure
struct halo_struct{
  int    ID;             //!< Halo ID
  int    Type;           //!< Type (0 for central, 1 for satellite)
  int    DescIndex;      //!< Index of descendant in next relevant snapshot
  int    TreeFlags;      //!< Bitwise flag indicating the type of match in the trees
  int    NSubgroups;     //!< Number of subgroups belonging to this type 0 (=-1 if type=1)
  struct halo_struct        *NextHaloInFOFGroup;
  struct galaxy_struct      *Galaxy;
  double Mvir;           //!< Bryan &Norman (ApJ 495, 80, 1998) virial mass [M_sol/h]
  int    Len;            //!< Number of particles in the structure
  float  Pos[3];         //!< Most bound particle position [Mpc/h]
  float  Vel[3];         //!< Centre-of-mass velocity [km/s]
  float  Rvir;           //!< Virial radius [Mpc/h]
  float  Rhalo;          //!< Distance of last halo particle from MBP [Mpc/h]
  float  Rmax;           //!< Radius of maximum circular velocity [Mpc/h]
  float  Vmax;           //!< Maximum circular velocity [km/s]
  float  VelDisp;        //!< Total 3D velocity dispersion [km/s]
  float  Spin[3];        //!< Specific angular momentum vector [Mpc/h *km/s]
};
typedef struct halo_struct halo_struct;

struct fof_group_struct{
  halo_struct *FirstHalo;
};
typedef struct fof_group_struct fof_group_struct;

struct galaxy_struct
{
  int    Type;
  int    HaloDescIndex;
  int    TreeFlags;
  struct halo_struct         *Halo;
  struct galaxy_struct       *NextGalInHalo;
  struct galaxy_struct       *Next;
  struct galaxy_struct       *MergerTarget;
  int    Len;

  // properties of subhalo at the last time this galaxy was a central galaxy
  double Pos[3];
  double Vel[3];
  double Mvir;
  double dM;
  double dMdt;
  double Rvir;
  double Vvir;
  double Vmax;

  // baryonic reservoirs
  double StellarMass;

  // misc
  double Sfr[NOUT];
  double DiskRadius;
  double Cos_Inc;
  double MergTime;
};
typedef struct galaxy_struct galaxy_struct;

struct galaxy_output_struct
{
  int   Type;
  int   HaloIndex;
  int   CentralGal;

  // properties of subhalo at the last time this galaxy was a central galaxy
  float Pos[3];
  float Vel[3];
  float Spin[3];
  int   Len;
  float Mvir;
  float dM;
  float dMdt;
  float Rvir;
  float Vvir;
  float Vmax;
  float VelDisp;

  // baryonic reservoirs
  float StellarMass;

  // misc
  float Sfr;
  float DiskRadius;
  float Cos_Inc;
  float MergTime;
};
typedef struct galaxy_output_struct galaxy_output_struct;


/*
 * Functions
 */

void myexit(int signum);
void read_parameter_file(run_globals_struct *run_globals, char *fname);
void init_meraxes(run_globals_struct *run_globals);
void dracarys(run_globals_struct *run_globals);
trees_header_struct read_halos(run_globals_struct *run_globals, int snapshot, halo_struct **halo, fof_group_struct **fof_group);
void free_halos(halo_struct **halo);
void init_galaxies(galaxy_struct *Gal, int n_halos_max);
void copy_halo_to_galaxy(run_globals_struct *run_globals, halo_struct *halo, galaxy_struct *gal);
double calculate_merging_time(run_globals_struct *run_globals, galaxy_struct *gal, int snapshot);

