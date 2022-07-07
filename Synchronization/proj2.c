/*      Jan Folenta
   IOS - 2.projekt 2017/2018
  xfolen00@stud.fit.vutbr.cz
*/     

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/stat.h>

#define MMAP(pointer) {(pointer) = mmap(NULL, sizeof(*(pointer)), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);}
#define UNMAP(pointer) {munmap((pointer), sizeof((pointer)));}

sem_t *writing = NULL;
sem_t *mutex = NULL;
sem_t *bus = NULL;
sem_t *boarded = NULL;
sem_t *finish = NULL;
sem_t *last_finish = NULL;

int *action_counter = NULL;
int *waiting = NULL;
int *boarded_rid = NULL;
int *get_on_board = NULL;
int *inside = NULL;
int *help_for_last_finish = NULL;

FILE *out_file;

void init_shared_vars();
void init_semaphores();
void process_bus(int capacity, int delay);
void process_rider(int i, int riders);
void kill_process();
void clean_all();	

// Funckia inicializuje zdielane premenne
// --------------------------------------------------------------------------------------
void init_shared_vars()
{
	MMAP(action_counter);
	MMAP(waiting);
	MMAP(boarded_rid);
	MMAP(get_on_board);
	MMAP(inside);
	MMAP(help_for_last_finish);
}

// Funkcia inicializuje semafory
// --------------------------------------------------------------------------------------
void init_semaphores()
{
	writing = sem_open("/xfolen00_sem_writing", O_CREAT | O_EXCL, 0666, 1);
	if (writing == SEM_FAILED)
	{
		fprintf(stderr, "Chyba: Chyba pri vytvarani semaforu\n");	
		exit(1);
	}
	
	mutex = sem_open("/xfolen00_sem_mutex", O_CREAT | O_EXCL, 0666, 1);
	if (mutex == SEM_FAILED)
	{
		fprintf(stderr, "Chyba: Chyba pri vytvarani semaforu\n");	
		exit(1);
	}
	
	bus = sem_open("/xfolen00_sem_bus", O_CREAT | O_EXCL, 0666, 0);
	if (bus == SEM_FAILED)
	{
		fprintf(stderr, "Chyba: Chyba pri vytvarani semaforu\n");	
		exit(1);
	}
	
	boarded = sem_open("/xfolen00_sem_boarded", O_CREAT | O_EXCL, 0666, 0);
	if (boarded == SEM_FAILED)
	{
		fprintf(stderr, "Chyba: Chyba pri vytvarani semaforu\n");	
		exit(1);
	}
	
	finish = sem_open("/xfolen00_sem_finish", O_CREAT | O_EXCL, 0666, 0);
	if (finish == SEM_FAILED)
	{
		fprintf(stderr, "Chyba: Chyba pri vytvarani semaforu\n");	
		exit(1);
	}
	
	last_finish = sem_open("/xfolen00_sem_last_finish", O_CREAT | O_EXCL, 0666, 0);
	if (last_finish == SEM_FAILED)
	{
		fprintf(stderr, "Chyba: Chyba pri vytvarani semaforu\n");	
		exit(1);
	}
}

typedef struct {
	int R;
	int C;
	int ART;
	int ABT;
} Arguments;

Arguments *args;

// Funkcia spracuje argumenty z prikazoveho riadku
// --------------------------------------------------------------------------------------
Arguments *parse_parameters(int argc, char *argv[])
{
	char *znaky;
	
	if (argc != 5)
	{
		fprintf(stderr, "Chyba: Nespravny pocet argumentov.\n");
		exit(1);
	}		

	int R = strtol(argv[1], &znaky, 10);	
	if (strlen(znaky) > 0 || R <= 0)
	{
		fprintf(stderr,"Chyba: Argument musi byt cele cislo -- R > 0.\n");
		exit(1);
	}
		
	int C = strtol(argv[2], &znaky, 10);	
	if (strlen(znaky) > 0 || C <= 0)
	{
		fprintf(stderr,"Chyba: Argument musi byt cele cislo -- C > 0.\n");
		exit(1);
	}
		
	int ART = strtol(argv[3], &znaky, 10);	
	if (strlen(znaky) > 0 || ART < 0 || ART > 1000)
	{
		fprintf(stderr,"Chyba: Argument musi byt cele cislo -- 0 <= ART <= 1000.\n");
		exit(1);
	}
	
	int ABT = strtol(argv[4], &znaky, 10);	
	if (strlen(znaky) > 0 || ABT < 0 || ABT > 1000)
	{
		fprintf(stderr,"Chyba: Argument musi byt cele cislo -- 0 <= ABT <= 1000.\n");
		exit(1);
	}
	
	Arguments *parse_parameters = NULL;
	parse_parameters = malloc(sizeof(Arguments));
	
	if (parse_parameters == NULL)
	{
		fprintf(stderr,"Chyba: Malloc sa nepodaril.\n");
		exit(1);
	}
	
	parse_parameters->R = R;
	parse_parameters->C = C;
	parse_parameters->ART = ART;
	parse_parameters->ABT = ABT;
	
	return parse_parameters;
}

// Funckia spracuva proces bus
// --------------------------------------------------------------------------------------
void process_bus(int capacity, int delay_bus)
{
	// -proces bus zacina
	sem_wait(writing);
	fprintf(out_file,"%d :BUS   :start\n", (*action_counter)++);
	sem_post(writing);
		
	while (*boarded_rid != 0)
	{
		// -autobus prichadza na zastavku
		sem_wait(mutex);
		
		sem_wait(writing);
		fprintf(out_file,"%d  :BUS   :arrival\n", (*action_counter)++);
		sem_post(writing);
		
		// -ak su na zastavke cestujuci, autobus ich zacne naberat
		if (*waiting > 0)
		{
			sem_wait(writing);
			fprintf(out_file,"%d  :BUS   :start boarding: %d\n", (*action_counter)++, *waiting);
			sem_post(writing);
		}
			
		// -autobus moze nabrat maximalne tolko cestujucich, aka je kapacita autobusu
		int n = ((*waiting) < capacity) ? *waiting : capacity;
		for (int i = 0; i < n; i++)
		{
			sem_post(bus);						
			sem_wait(boarded);
		}	
			
		*waiting = (*waiting) > 0 ? *waiting : 0;			
			
		// -ak niekto nastupil do autobusu, vypise sa 'end boarding'
		if (*get_on_board > 0)
		{
			sem_wait(writing);
			fprintf(out_file,"%d  :BUS   :end boarding: %d\n", (*action_counter)++, *waiting);
			sem_post(writing);
			*get_on_board = 0;
		}
			
		// -autobus odchadza
		sem_wait(writing);		
		fprintf(out_file,"%d  :BUS   :depart\n", (*action_counter)++);
		sem_post(writing);
		
		sem_post(mutex);
		
		// -autobus simuluje svoju jazdu tym, ze sa uspi
		if (args->ABT > 0)
			usleep(rand()%(delay_bus)*1000);
		else 
			usleep(0);			
			
		// -autobus ukonci svoju jazdu			
		sem_wait(writing);
		fprintf(out_file,"%d  :BUS   :end\n", (*action_counter)++);
		sem_post(writing);

		for (int j = 0; j < *inside; j++)
			sem_post(finish);					
	}	
		
	sem_wait(last_finish);
		
	// -proces bus konci	
	sem_wait(writing);
	fprintf(out_file,"%d  :BUS   :finish\n", (*action_counter)++);
	sem_post(writing);
			
	clean_all();			
	exit(0);
}

// Funckia spracuva proces rider
// --------------------------------------------------------------------------------------
void process_rider(int i, int riders)
{
	int rid_id = i + 1;
				
	// -proces rider zacina	
	sem_wait(writing);
	fprintf(out_file,"%d  :RID %d   :start\n", (*action_counter)++, rid_id);
	sem_post(writing);
				
	// -ak na zastavke nie je autobus, tak cestujuci vstupuje na zastavku
	sem_wait(mutex);
					
	(*waiting)++;
								
	sem_wait(writing);
	fprintf(out_file,"%d  :RID %d   :enter: %d\n", (*action_counter)++, rid_id, *waiting);
	sem_post(writing);
					
	sem_post(mutex);		
				
	// -autobus stoji a cestujuci nastupuju do autobusu	
	sem_wait(bus);			
					
	sem_wait(writing);
	fprintf(out_file,"%d  :RID %d   :boarding\n", (*action_counter)++,  rid_id);
	sem_post(writing);					
					
	(*inside)++;
	(*get_on_board)++;
	(*waiting)--;
	(*boarded_rid)--;					
					
	sem_post(boarded);
					
	sem_wait(finish);					
				
	// -proces rider konci	
	sem_wait(writing);
	fprintf(out_file,"%d  :RID %d   :finish\n", (*action_counter)++, rid_id);
	(*inside)--;
	sem_post(writing);
				
	// -aby sa proces bus neskoncil skor ako posledny cestujuci	
	(*help_for_last_finish)++;
					
	if (*help_for_last_finish == riders)
		sem_post(last_finish);
				
	clean_all();
	exit(0);
}

// Funkcia zabije proces
// --------------------------------------------------------------------------------------
void kill_process()
{
	kill(getpid(), SIGTERM);
	clean_all();
	exit(1);
}

// Funkcia za sebou poupratuje
// --------------------------------------------------------------------------------------
void clean_all()
{
	sem_close(writing);
	sem_close(mutex);
	sem_close(bus);
	sem_close(boarded);
	sem_close(finish);
	sem_close(last_finish);
	
	sem_unlink("/xfolen00_sem_writing");
	sem_unlink("/xfolen00_sem_mutex");
	sem_unlink("/xfolen00_sem_bus");
	sem_unlink("/xfolen00_sem_boarded");
	sem_unlink("/xfolen00_sem_finish");
	sem_unlink("/xfolen00_sem_last_finish");
	
	UNMAP(action_counter);
	UNMAP(waiting);
	UNMAP(boarded_rid);
	UNMAP(get_on_board);
	UNMAP(inside);
	UNMAP(help_for_last_finish);
	
	free(args);
	
	fclose(out_file);
}

//***************************************************************************************

int main(int argc, char *argv[])
{
	args = parse_parameters(argc, argv);
	
	init_shared_vars();
	init_semaphores();
	
	out_file = fopen("proj2.out","w");
	if (out_file == NULL)
	{
		fprintf(stderr,"Chyba: Nepodarilo sa otvorit subor proj2.out.\n");
		clean_all();
		exit(1);
	}
		
	setbuf(out_file, NULL);
	
	srand(time(0));

	signal(SIGINT, kill_process);
	signal(SIGTERM, kill_process);
	
	*action_counter = 1;
	*waiting = 0;
	*boarded_rid = args->R;
	*get_on_board = 0;
	*inside = 0;
	*help_for_last_finish = 0;

	pid_t busPID;
	pid_t generatorPID;
	pid_t riderPID[args->R];

//  	PROCES BUS	
//  ------------------------------------------------------------------------------------
	busPID = fork();
	if (busPID == -1)
	{
		fprintf(stderr, "Chyba: Fork procesu neprebehol uspesne.\n");
		kill(busPID, SIGTERM);
		kill_process();
		exit (1);
	}
	
	else if (busPID == 0)
	{	
		process_bus(args->C, args->ABT);	
	}
	
	else 
	{
		// nic sa nedeje
	}

//  	PROCES na generovanie riders 
//  -------------------------------------------------------------------------------------
	generatorPID = fork();
	if (generatorPID == -1)
	{
		fprintf(stderr, "Chyba: Fork procesu neprebehol uspesne\n");
		kill(generatorPID, SIGTERM);
		kill(busPID, SIGTERM);
		kill_process();
		exit (1);
	}
		
	else if (generatorPID == 0)
	{
		for (int i = 0; i < args->R; i++)
		{		
			//		PROCES RIDER
			// ----------------------------------------------------------------------------
		
			pid_t riderPID[i];
			riderPID[i] = fork();
			if (riderPID[i] == -1)
			{
				fprintf(stderr, "Chyba: Fork procesu neprebehol uspesne\n");
				kill(generatorPID, SIGTERM);
				kill_process();
				exit (1);
			}
				
			else if (riderPID[i] == 0)
			{
				process_rider(i, args->R);
			}
				
			else 
			{
				// nic sa nedeje
			}
			
			// -pred vygenerovanim dalsieho cestujuceho sa proces uspi	
			if (args->ART > 0)
				usleep(rand()%(args->ART)*1000);
			else
				usleep(0);
		}
			
		for (int i = 0; i < args->R; i++)
			waitpid(riderPID[i], NULL, 0);
			
		clean_all();
		exit(0);
	} 
	
	else 
	{
		// nic sa nedeje
	} 
			
	waitpid(busPID, NULL, 0);
	waitpid(generatorPID, NULL, 0);
	
	clean_all();	
	return 0;
}
