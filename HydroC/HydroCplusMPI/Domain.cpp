//
// (C) Guillaume.Colin-de-Verdiere at CEA.Fr
//
#ifdef MPI_ON
#include <mpi.h>
#endif
#ifdef _OPENMP
#include <omp.h>
#endif

#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cerrno>
#include <iostream>
#include <iomanip>

#include <strings.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <float.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <stdarg.h>

using namespace std;

//
#include "EnumDefs.hpp"
#include "Domain.hpp"
#include "cclock.h"

// template <typename T> 
// Domain::Domain(void) { }
// template <typename T> 
Domain::Domain(int argc, char **argv)
{
	// default values
	m_nextOutput = 0;
	m_nextImage = 0;	
	m_nbRun = 0;
	m_elapsTotal = 0.0;
	m_dtImage = 0;
	m_iter = 0;
	m_checkPoint = 0;
	m_withPng = 0;
	m_npng = 0;
	m_shrink = 1;
	m_shrinkSize = 2000;
#if WITHPNG > 0
	m_buffer = 0;
#endif
	m_prt = 0;
	m_inputFile = 0;
	m_withMorton = 0;
	m_ExtraLayer = 2;
	m_globNx = 128;
	m_globNy = 128;
	m_tileSize = 16;
	m_nbtiles = 1;
	m_dx = 0.05;
	m_boundary_left = 1;
	m_boundary_right = 1;
	m_boundary_down = 1;
	m_boundary_up = 1;
	m_nIterRiemann = 10;
	m_testcase = 0;
	m_cfl = 0.8;
	m_nStepMax = 0;
	m_nOutput = 0;
	m_nvtk = 0;
	m_tend = 100;
	m_tcur = 0.0;
	m_dt = 0;
	m_dx = 0.05;
	m_dtOutput = 0;
	m_scan = X_SCAN;
	m_scheme = SCHEME_MUSCL;
	m_tiles = 0;
	m_localDt = 0;
	m_gamma = 1.4;
	m_smallc = 1e-10;
	m_smallr = 1e-10;
	m_iorder = 2;
	m_slope_type = 1.;
	m_recvbufru = 0;	// receive right or up
	m_recvbufld = 0;	// receive left or down
	m_sendbufru = 0;	// send right or up
	m_sendbufld = 0;	// send left or down
	m_numThreads = 1;	// runs serially by default


#ifdef MPI_ON
	MPI_Init(&argc, &argv);
#endif

	initMPI();
	double tRemain = m_tr.timeRemainAll();
	m_timeGuard = 800;
	if (tRemain < 36000 ) m_timeGuard = 600;
	if (tRemain < 3600  ) m_timeGuard = 120;
	if (tRemain < 1800  ) m_timeGuard =  60;
	if (tRemain < 60    ) m_timeGuard =  10;

	parseParams(argc, argv);
	readInput();
	domainDecompose();

	createTestCase(); // will be overwritten if in the middle of test case

	if (hasProtection()) {
		double start, end;
		start = dcclock();
		readProtection();
		end = dcclock();
		if (m_myPe == 0) {
			char txt[256];
			double elaps = (end - start);
			convertToHuman(txt, elaps);
			cerr << "Read protection in " << txt << " (" << elaps << "s)"<<endl;
		}
	}

	if ((m_nOutput > 0) || (m_dtOutput > 0)) {
		vtkOutput(m_nvtk);
		m_nvtk++;
	}
	setTiles();
	if (m_myPe == 0) {
		printSummary();
	}
}

Domain::~Domain()
{
	delete m_uold;
	if (m_tiles) {
		for (uint32_t i = 0; i < m_nbtiles; i++) {
			if (m_tiles[i]) delete m_tiles[i];
		}
		delete [] m_tiles;
	}
	if (m_recvbufru) free(m_recvbufru);
	if (m_recvbufld) free(m_recvbufld);
	if (m_sendbufru) free(m_sendbufru);
	if (m_sendbufld) free(m_sendbufld);
	if (m_localDt) free(m_localDt);

	for (uint32_t i = 0; i < m_numThreads; i++) {
		delete m_buffers[i];
	}
	delete [] m_buffers;
	delete [] m_mortonIdx;
	delete m_morton;
	if (m_inputFile) free(m_inputFile); 
	cerr << "End ~Domain " << getMype() << endl;
}

void
 Domain::domainDecompose()
{
	uint32_t xmin, xmax, ymin, ymax;
	uint32_t lgx, lgy, lgmax;

	m_nx = m_globNx;
	m_ny = m_globNy;

	m_box[XMIN_D] = 0;
	m_box[XMAX_D] = m_nx;
	m_box[YMIN_D] = 0;
	m_box[YMAX_D] = m_ny;
	m_box[LEFT_D] = -1;
	m_box[RIGHT_D] = -1;
	m_box[DOWN_D] = -1;
	m_box[UP_D] = -1;

	if (m_nProc > 1) {
		CalcSubSurface(0, m_globNx, 0, m_globNy, 0, m_nProc - 1, m_box,
			       m_myPe);

		m_nx = m_box[XMAX_D] - m_box[XMIN_D];
		m_ny = m_box[YMAX_D] - m_box[YMIN_D];

		// adapt the boundary conditions 
		if (m_box[LEFT_D] != -1) {
			m_boundary_left = 0;
		}
		if (m_box[RIGHT_D] != -1) {
			m_boundary_right = 0;
		}
		if (m_box[DOWN_D] != -1) {
			m_boundary_down = 0;
		}
		if (m_box[UP_D] != -1) {
			m_boundary_up = 0;
		}

		if (m_prt) {
			cout << m_globNx << " ";
			cout << m_globNy << " ";
			cout << m_nProc << " ";
			cout << m_myPe << " - ";
			cout << m_nx << " ";
			cout << m_ny << " - ";
			cout << m_box[LEFT_D] << " ";
			cout << m_box[RIGHT_D] << " ";
			cout << m_box[DOWN_D] << " ";
			cout << m_box[UP_D] << endl;
		}
	}

	getExtends(TILE_FULL, xmin, xmax, ymin, ymax);

	lgx = (xmax - xmin);
	lgy = (ymax - ymin);
	lgmax = lgx;
	if (lgmax < lgy)
		lgmax = lgy;

	lgmax *= m_ExtraLayer;

	m_recvbufru = AlignedAllocReal(lgmax * NB_VAR);
	memset(m_recvbufru, 0, lgmax * NB_VAR * sizeof(real_t));
	m_recvbufld = AlignedAllocReal(lgmax * NB_VAR);
	memset(m_recvbufld, 0, lgmax * NB_VAR * sizeof(real_t));
	m_sendbufru = AlignedAllocReal(lgmax * NB_VAR);
	memset(m_sendbufru, 0, lgmax * NB_VAR * sizeof(real_t));
	m_sendbufld = AlignedAllocReal(lgmax * NB_VAR);
	memset(m_sendbufld, 0, lgmax * NB_VAR * sizeof(real_t));
}


void Domain::printSummary()
{
	if (m_myPe == 0) {
		printf("+-------------------+\n");
		printf("|GlobNx=%-7d     |\n", m_globNx);
		printf("|GlobNy=%-7d     |\n", m_globNy);
		printf("|nx=%-7d         |\n", m_nx);
		printf("|ny=%-7d         |\n", m_ny);
#if TILEUSER == 1
		printf("|ts=%-7d         |\n", m_tileSize);
#else
		printf("|ts=%-7d         |\n", TILESIZ);
#endif
		printf("|nt=%-7d         |\n", m_nbtiles);
		printf("|morton=%-7u     |\n", m_withMorton);
		printf("|tend=%-10.3f    |\n", m_tend);
		printf("|nstepmax=%-7d   |\n", m_nStepMax);
		printf("|noutput=%-7d    |\n", m_nOutput);
		printf("|dtoutput=%-10.3f|\n", m_dtOutput);
		printf("|dtimage=%-10.3f|\n", m_dtImage);
		printf("+-------------------+\n");
	}
}

static void keyval(char *buffer, char **pkey, char **pval)
{
	char *ptr;
	*pkey = buffer;
	*pval = buffer;

	// kill the newline
	*pval = strchr(buffer, '\n');
	if (*pval)
		**pval = 0;

	// suppress leading whites or tabs
	while ((**pkey == ' ') || (**pkey == '\t'))
		(*pkey)++;
	*pval = strchr(buffer, '=');
	if (*pval) {
		**pval = 0;
		(*pval)++;
	}
	// strip key from white or tab
	while ((ptr = strchr(*pkey, ' ')) != NULL) {
		*ptr = 0;
	}
	while ((ptr = strchr(*pkey, '\t')) != NULL) {
		*ptr = 0;
	}
}

void Domain::readInput()
{
	FILE *fd = NULL;
	char buffer[1024];
	char *pval, *pkey;
	char *realFmt;

	if (sizeof(real_t) == sizeof(double)) {
	  realFmt = (char *)("%lf");
	} else {
	  realFmt = (char *)("%f");
	}

	fd = fopen(m_inputFile, "r");
	if (fd == NULL) {
		cerr << "can't read input file\n" << endl;
		abort();
	}
	while (fgets(buffer, 1024, fd) == buffer) {
		keyval(buffer, &pkey, &pval);

		// int parameters
		if (strcmp(pkey, "nstepmax") == 0) {
			sscanf(pval, "%d", &m_nStepMax);
			continue;
		}
		if (strcmp(pkey, "chkpt") == 0) {
			sscanf(pval, "%d", &m_checkPoint);
			continue;
		}
		if (strcmp(pkey, "prt") == 0) {
			sscanf(pval, "%d", &m_prt);
			continue;
		}
		if (strcmp(pkey, "nx") == 0) {
			sscanf(pval, "%d", &m_globNx);
			continue;
		}
		if (strcmp(pkey, "ny") == 0) {
			sscanf(pval, "%d", &m_globNy);
			continue;
		}
		if (strcmp(pkey, "tilesize") == 0) {
			sscanf(pval, "%d", &m_tileSize);
			continue;
		}
		if (strcmp(pkey, "boundary_left") == 0) {
			sscanf(pval, "%d", &m_boundary_left);
			continue;
		}
		if (strcmp(pkey, "boundary_right") == 0) {
			sscanf(pval, "%d", &m_boundary_right);
			continue;
		}
		if (strcmp(pkey, "boundary_up") == 0) {
			sscanf(pval, "%d", &m_boundary_up);
			continue;
		}
		if (strcmp(pkey, "boundary_down") == 0) {
			sscanf(pval, "%d", &m_boundary_down);
			continue;
		}
		if (strcmp(pkey, "niter_riemann") == 0) {
			sscanf(pval, "%d", &m_nIterRiemann);
			continue;
		}
		if (strcmp(pkey, "noutput") == 0) {
			sscanf(pval, "%d", &m_nOutput);
			continue;
		}
		if (strcmp(pkey, "iorder") == 0) {
			sscanf(pval, "%d", &m_iorder);
			continue;
		}
		if (strcmp(pkey, "morton") == 0) {
			sscanf(pval, "%d", &m_withMorton);
			continue;
		}
		// float parameters
		if (strcmp(pkey, "slope_type") == 0) {
			// sscanf(pval, realFmt, &H->slope_type);
			continue;
		}
		if (strcmp(pkey, "tend") == 0) {
			sscanf(pval, realFmt, &m_tend);
			continue;
		}
		if (strcmp(pkey, "dx") == 0) {
			sscanf(pval, realFmt, &m_dx);
			continue;
		}
		if (strcmp(pkey, "courant_factor") == 0) {
			sscanf(pval, realFmt, &m_cfl);
			continue;
		}
		if (strcmp(pkey, "smallr") == 0) {
			// sscanf(pval, realFmt, &H->smallr);
			continue;
		}
		if (strcmp(pkey, "smallc") == 0) {
			// sscanf(pval, realFmt, &H->smallc);
			continue;
		}
		if (strcmp(pkey, "dtoutput") == 0) {
			sscanf(pval, realFmt, &m_dtOutput);
			continue;
		}
		if (strcmp(pkey, "dtimage") == 0) {
			sscanf(pval, realFmt, &m_dtImage);
			continue;
		}
		if (strcmp(pkey, "testcase") == 0) {
			sscanf(pval, "%d", &m_testcase);
			continue;
		}
		// string parameter
		if (strcmp(pkey, "scheme") == 0) {
			if (strcmp(pval, "muscl") == 0) {
				m_scheme = SCHEME_MUSCL;
			} else if (strcmp(pval, "plmde") == 0) {
				m_scheme = SCHEME_PLMDE;
			} else if (strcmp(pval, "collela") == 0) {
				m_scheme = SCHEME_COLLELA;
			} else {
				cerr <<
				    "Scheme name <%s> is unknown, should be one of [muscl,plmde,collela]\n"
				    << pval << endl;
				abort();
			}
			continue;
		}
	}
}

void Domain::parseParams(int argc, char **argv)
{
	uint32_t n = 1;
	while (n < argc) {
		if (strcmp(argv[n], "--help") == 0) {
			// usage();
			n++;
			continue;
		}
		if (strcmp(argv[n], "-v") == 0) {
			n++;
			continue;
		}
		if (strcmp(argv[n], "-i") == 0) {
			n++;
			m_inputFile = strdup(argv[n]);
			n++;
			continue;
		}
		std::cerr << "Key " << argv[n] << " is unkown\n" << std::endl;
		n++;
	}
}

void Domain::setTiles()
{
	uint32_t i, j, offx, offy, tileSizeX, tileSizeY, tileSize, mortonW, mortonH;
	Matrix2 <uint32_t> *mortonIdx; // to hold the array of tiles ids.
	//
	m_nbtiles = 0;
	tileSize = m_tileSize;
#if TILEUSER == 1
	if (tileSize <= 0) {
#ifdef _OPENMP
		m_nbtiles = omp_get_max_threads();
		tileSize = sqrt(m_nx * m_ny / m_nbtiles);
		m_tileSize = tileSize;
#else
		tileSize = 128;
#endif
		if (!m_myPe)
			cout << "Computing tilesize to " << tileSize << endl;
	} else {
		if (!m_myPe)
			cout << "Forcing tilesize to " << tileSize << endl;
	}
#else
	tileSize = TILESIZ;
	if ((tileSize > m_nx) && (tileSize > m_ny)) {
		tileSize = max(m_nx, m_ny);
	}
	if (!m_myPe)
		cout << "Forcing tilesize to " << tileSize << " at compile time" << endl;
#endif

	m_nbtiles = 0;
	mortonH = 0;
	for (j = 0; j < m_ny; j += tileSize) {
		mortonW = 0;
		for (i = 0; i < m_nx; i += tileSize) {
			m_nbtiles++;
			mortonW++;
		}
		mortonH++;
	}

	m_localDt = AlignedAllocReal(m_nbtiles);
	m_tiles = new Tile *[m_nbtiles];
#pragma omp parallel for private(i)
	for (uint32_t i = 0; i < m_nbtiles; i++) {
		m_tiles[i] = new Tile;
	}
	// Create the Morton holder to wander around the tiles
	m_morton = new Matrix2 <uint32_t> (mortonW, mortonH);
	// cerr << mortonW << " " << mortonH << endl;
	m_mortonIdx = m_morton->listMortonIdx();
	assert(m_mortonIdx != 0);

	m_nbtiles = 0;
	mortonH = 0;
	for (j = 0; j < m_ny; j += tileSize) {
		mortonW = 0;
		for (i = 0; i < m_nx; i += tileSize) {
			(*m_morton)(mortonW, mortonH) = m_nbtiles;
			m_nbtiles++;
			mortonW++;
		}
		mortonH++;
	}
	//
	m_nbtiles = 0;
	offy = 0;
	for (j = 0; j < m_ny; j += tileSize) {
		tileSizeY = tileSize;
		if (offy + tileSizeY >= m_ny)
			tileSizeY = m_ny - offy;
		assert(tileSizeY <= tileSize);
		offx = 0;
		for (i = 0; i < m_nx; i += tileSize) {
			tileSizeX = tileSize;
			if (offx + tileSizeX >= m_nx)
				tileSizeX = m_nx - offx;
			assert(tileSizeX <= tileSize);
			m_tiles[m_nbtiles]->setPrt(m_prt);
			m_tiles[m_nbtiles++]->setExtend(tileSizeX, tileSizeY,
							m_nx, m_ny, offx, offy,
							m_dx);
			if (m_prt) {
				cout << "tsx " << tileSizeX << " tsy " <<
				    tileSizeY;
				cout << " ofx " << offx << " offy " << offy;
				cout << " nx " << m_nx << " m_ny " << m_ny <<
				    endl;
			}
			offx += tileSize;
		}
		offy += tileSize;
	}

	// Create the shared buffers
#ifdef _OPENMP
	m_numThreads = omp_get_max_threads();
#else
	m_numThreads = 1;
#endif
	uint32_t tileSizeTot = tileSize + 2 * m_ExtraLayer;
	m_buffers = new ThreadBuffers *[m_numThreads];
#pragma omp parallel for private(i)
	for (uint32_t i = 0; i < m_numThreads; i++) {
		m_buffers[i] =
		    new ThreadBuffers(0, tileSizeTot, 0, tileSizeTot);
	}

	for (uint32_t i = 0; i < m_nbtiles; i++) {
		m_tiles[i]->initTile(m_uold);
		m_tiles[i]->setMpi(m_nProc, m_myPe);
		m_tiles[i]->initPhys(m_gamma, m_smallc, m_smallr, m_cfl,
				     m_slope_type, m_nIterRiemann, m_iorder,
				     m_scheme);
		m_tiles[i]->setScan(X_SCAN);
	}
	if (!m_myPe) {
		char hostn[1024];
		char cpuName[1024];
		memset(hostn, 0, 1024);
		memset(cpuName, 0, 1024);

		gethostname(hostn, 1023);
		cout << "Starting run with " << m_nbtiles << " tiles";
		cout << " on " << hostn << endl;
		getCPUName(cpuName);
		cout << "CPU name" << cpuName << endl;
	}
	// exit(0);
}

void Domain::initMPI()
{
#ifdef MPI_ON
	int nproc, mype;
	MPI_Comm_size(MPI_COMM_WORLD, &nproc);
	MPI_Comm_rank(MPI_COMM_WORLD, &mype);
	m_nProc = nproc;
	m_myPe = mype;
#else
	m_myPe = 0;
	m_nProc = 1;
#endif
}
// EOF
