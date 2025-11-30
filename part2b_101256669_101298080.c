#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <time.h>
#include <fcntl.h>
#include <signal.h>

/**
 * @author Wenxuan Han 101256669
 * @author Tony Yao 101298080
 */

#define EXAM_AMOUNT_MOST 20
#define QUESTION_AMOUNT_EXAM 5
#define MAX_STUDENT_FOR_EXAM 200
#define MAX_STUDENT_NUMBER_IN_EXAM_PILES 9999

// add elements of semaphore
#define SEM_KEY 1234
#define SEMAPHORE_RUBRIC 0    // for rubric modification for each Ta
#define SEMAPHORE_EXAM_LOAD 1 // for loading the next exam to share memory
#define SEMAPHORE_QUESTION 2  // for number of questions that being selected

#ifndef __APPLE__
union semun
{
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};
#endif

typedef struct
{
    int current_student_number;           // student id
    char rubric[QUESTION_AMOUNT_EXAM];    // grade letter on rubric for exam paper questions
    int exam_marks[QUESTION_AMOUNT_EXAM]; // 0 = unmarked; 1 = still in progress 2 = marked
    int exam_index;                       // index into exam files
    int finished;                         // set to 1 to finish

} Shared_Memory;

// add some functions of semaphore
void semaphore_wait(int semaphore_Id, int semaphore_number)
{
    struct sembuf operation;
    operation.sem_num = semaphore_number; // number of semaphore
    operation.sem_op = -1;                // decrease operation or wait
    operation.sem_flg = 0;                // Blocking operation
    if (semop(semaphore_Id, &operation, 1) < 0)
    {
        perror("semop wait was failed!!!!!!!!.\n");
    }
}

void semaphore_signal(int semaphore_Id, int semaphore_number)
{
    struct sembuf operation;
    operation.sem_num = semaphore_number; // number of semaphore
    operation.sem_op = 1;                 // increase operation or signal
    operation.sem_flg = 0;                // Blocking operation
    if (semop(semaphore_Id, &operation, 1) < 0)
    {
        perror("semop signal was failed!!!!!!!!.\n");
    }
}

int semaphore_initialization()
{
    int semid = semget(SEM_KEY, 3, 0666 | IPC_CREAT);
    if (semid < 0)
    {
        perror("semget failed in that way");
        exit(1);
    }

    // Initialize all semaphores to 1
    union semun arg;
    unsigned short values[3] = {1, 1, 1};
    arg.array = values;

    if (semctl(semid, 0, SETALL, arg) < 0)
    {
        perror("semctl initialization failed in that way");
        exit(1);
    }

    printf("Semaphores has been initialized successfully.\n");
    return semid;
}

void load_rubric_into_shared_memory(Shared_Memory *shared_memory, const char *file_name)
{
    FILE *file = fopen(file_name, "r");
    if (file == NULL)
    {
        perror("there was an error in opening the rubric file");
        exit(1);
    }

    int first_number_with_rubric;
    char comma_in_middle_rubric;
    char character_after_comma;

    for (int i = 0; i < QUESTION_AMOUNT_EXAM; i++)
    {
        if (fscanf(file, "%d %c %c", &first_number_with_rubric, &comma_in_middle_rubric, &character_after_comma) != 3 || comma_in_middle_rubric != ',')
        {
            fprintf(stderr, "there was a format wrong error in the rubric on the specific line of %d\n", i + 1);
            fclose(file);
            exit(1);
        }

        if (first_number_with_rubric > 5 || first_number_with_rubric < 1)
        {
            fprintf(stderr, "the number of questions on exam rubric is out of the range compared to questions numbers on exam paper \n");
            fclose(file);
            exit(1);
        }

        shared_memory->rubric[first_number_with_rubric - 1] = character_after_comma;
    }

    fclose(file);
}

void load_exam_paper_into_shared_memory(Shared_Memory *shared_memory, const char *file_name_from_exam_piles)
{
    FILE *file = fopen(file_name_from_exam_piles, "r");
    if (file == NULL)
    {
        perror("there was an error in opening the rubric file");
        exit(1);
    }

    int student_number;
    if (fscanf(file, "%d", &student_number) != 1)
    {
        fprintf(stderr, "there was wrong error or missed student number in the exam pile with student of %s\n", file_name_from_exam_piles);
        fclose(file);
        exit(1);
    }

    fclose(file);

    shared_memory->current_student_number = student_number;

    for (int i = 0; i < QUESTION_AMOUNT_EXAM; i++)
    {
        shared_memory->exam_marks[i] = 0; // assume all exam question was not marked after loading into the shared memory
    }

    if (shared_memory->current_student_number == MAX_STUDENT_NUMBER_IN_EXAM_PILES)
    {
        shared_memory->finished = 1;
    }
}

void Save_modified_rubric(Shared_Memory *shared_memory, const char *file_name)
{
    FILE *file = fopen(file_name, "r");
    if (file == NULL)
    {
        perror("there was an error in opening the rubric file");
        return;
    }

    for (int i = 0; i < QUESTION_AMOUNT_EXAM; i++)
    {
        fprintf(file, "%d, %c\n", i + 1, shared_memory->rubric[i]);
    }
    fclose(file);
    printf("Rubric file updated successfully\n");
}

int question_number_on_exam_selected(Shared_Memory *shared_memory, int semid)
{
    int number = -1;
    semaphore_wait(semid, SEMAPHORE_QUESTION);

    for (int i = 0; i < QUESTION_AMOUNT_EXAM; i++)
    {
        if (shared_memory->exam_marks[i] == 0)
        {
            shared_memory->exam_marks[i] = 1;
            number = i + 1;
            break;
        }
    }
    semaphore_signal(semid, SEMAPHORE_QUESTION);
    return number;
}

void TA_process(Shared_Memory *shared_memory, int TA_ID_number, int number_exam, char exam_paper_files[][MAX_STUDENT_FOR_EXAM], const char *rubric_file, int semid)
{
    srand(time(NULL) ^ getpid());

    while (1)
    {
        if (shared_memory->finished)
        {
            printf("TA %d has finished marking the exam. And time to exit. \n", TA_ID_number);
            break;
        }

        if (shared_memory->current_student_number == MAX_STUDENT_NUMBER_IN_EXAM_PILES)
        {
            printf("TA %d mark the exam with the student id of 9999 and process should terminate.\n", TA_ID_number);
            break;
        }

        printf("TA %d starts marking the exam paper for student of %d\n", TA_ID_number, shared_memory->current_student_number);
        fflush(stdout);

        // steps to let TA to check the rubric (need to make sure only one TA can modify)
        for (int i = 0; i < QUESTION_AMOUNT_EXAM; i++)
        {
            usleep(500000 + (rand() % 500001));

            int rubric_need_change = rand() % 2; // value of one or zero to determine if need to change the rubric
            if (rubric_need_change)
            {
                semaphore_wait(semid, SEMAPHORE_RUBRIC);

                char original_letter_char = shared_memory->rubric[i];
                char new_letter_char = original_letter_char + 1;
                shared_memory->rubric[i] = new_letter_char;

                printf("TA %d has finished correcting rubric in the question of %d with the new letter of %c.\n", TA_ID_number, i + 1, shared_memory->rubric[i]);
                fflush(stdout);

                Save_modified_rubric(shared_memory, rubric_file);

                semaphore_signal(semid, SEMAPHORE_RUBRIC);
            }
        }

        // steps for ta to mark the questions
        while (1)
        {
            int question_number = question_number_on_exam_selected(shared_memory, semid);
            if (question_number == -1)
            {
                break;
            }

            printf("TA %d starts marking the questions of number %d for student %d.\n", TA_ID_number, question_number, shared_memory->current_student_number);
            fflush(stdout);

            usleep(1000000 + (rand() % 1000001));

            semaphore_wait(semid, SEMAPHORE_QUESTION);

            shared_memory->exam_marks[question_number - 1] = 2;

            semaphore_signal(semid, SEMAPHORE_QUESTION);

            printf("TA %d has finished marking the questions of %d for student %d.\n", TA_ID_number, question_number, shared_memory->current_student_number);
            fflush(stdout);
        }

        semaphore_wait(semid, SEMAPHORE_EXAM_LOAD); // used for protecting exam

        if (!shared_memory->finished)
        {
            printf("TA %d has finished marking all questions for student %d. Now one TA can loading the next exam paper.\n", TA_ID_number, shared_memory->current_student_number);
            fflush(stdout);

            shared_memory->exam_index++;
            if (shared_memory->exam_index >= number_exam)
            {
                shared_memory->finished = 1;

                semaphore_signal(semid, SEMAPHORE_EXAM_LOAD);

                break;
            }

            load_exam_paper_into_shared_memory(shared_memory, exam_paper_files[shared_memory->exam_index]);
            printf("TA %d is loading exam for student %d\n", TA_ID_number, shared_memory->current_student_number);
            fflush(stdout);

        }
        semaphore_signal(semid, SEMAPHORE_EXAM_LOAD);

        usleep(100000);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        printf("Usage: %s <number_of_TAs_marking_exam_concurrently>\n", argv[0]);
        exit(1);
    }
    int number_TAs = atoi(argv[1]);

    if (number_TAs < 2)
    {
        printf("Error warnning!!!!!!!!!!!!!!!!!! Number of TAs must be at least 2\n");
        exit(1);
    }

    printf("Start Marking exam system with %d TAs\n", number_TAs);
    fflush(stdout);

    // semapores initialization
    int semid = semaphore_initialization();

    const char *rubric_file = "rubric.txt";
    const char *exam_paper_collection_file = "all_exam_files.txt";

    char exam_files[EXAM_AMOUNT_MOST][MAX_STUDENT_FOR_EXAM];
    int actual_exam_count = 0;

    FILE *file = fopen(exam_paper_collection_file, "r");
    if (file == NULL)
    {
        perror("there was an error in opening the exam paper collection file.\n");

        semctl(semid, 0, IPC_RMID); // clean up semaphores before exit(1)

        exit(1);
    }

    while (fgets(exam_files[actual_exam_count], MAX_STUDENT_FOR_EXAM, file) && actual_exam_count < EXAM_AMOUNT_MOST)
    {
        exam_files[actual_exam_count][strcspn(exam_files[actual_exam_count], "\r\n")] = '\0';
        if (exam_files[actual_exam_count][0] == '\0')
        {
            continue;
        }
        actual_exam_count = actual_exam_count + 1;
    }
    fclose(file);

    if (actual_exam_count == 0)
    {
        fprintf(stderr, "there was nothing information on the exam list file all_exam_files.txt.\n");
        semctl(semid, 0, IPC_RMID); // clean up semaphores before exit(1)
        exit(1);
    }

    printf("Loaded %d exam file paths\n", actual_exam_count);

    // ways to create shared memory

    int shmid = shmget(IPC_PRIVATE, sizeof(Shared_Memory), 0666 | IPC_CREAT);
    if (shmid < 0)
    {
        perror("shmget failed");
        semctl(semid, 0, IPC_RMID); // clean up semaphores before exit(1)
        exit(1);
    }

    printf("Shared memory created with ID: %d\n", shmid);

    // Attach shared memory
    Shared_Memory *shared_memory = (Shared_Memory *)shmat(shmid, NULL, 0);
    if (shared_memory == (void *)-1)
    {
        perror("shmat failed");
        semctl(semid, 0, IPC_RMID); // clean up semaphores before exit(1)
        shmctl(shmid, IPC_RMID, NULL);
        exit(1);
    }

    printf("the Shared memory was being attached successfully\n");
    fflush(stdout);

    // Initialize shared memory
    shared_memory->exam_index = 0;
    shared_memory->finished = 0;
    for (int i = 0; i < QUESTION_AMOUNT_EXAM; i++)
    {
        shared_memory->exam_marks[i] = 0;
    }

    // Load initial rubric and exam
    load_rubric_into_shared_memory(shared_memory, rubric_file);
    load_exam_paper_into_shared_memory(shared_memory, exam_files[0]);

    printf("current student in share memory: %d\n", shared_memory->current_student_number);
    // Create TA processes
    pid_t pids[number_TAs];

    for (int i = 0; i < number_TAs; i++)
    {
        pids[i] = fork();

        if (pids[i] == 0)
        {
            // Child process - TA
            printf("TA %d started (PID: %d)\n", i + 1, getpid());
            fflush(stdout);

            // Run TA process
            TA_process(shared_memory, i + 1, actual_exam_count, exam_files, rubric_file, semid);

            // Cleanup and exit
            printf("TA %d (PID: %d) completed work\n", i + 1, getpid());
            fflush(stdout);
            shmdt(shared_memory);
            exit(0);
        }
        else if (pids[i] < 0)
        {
            perror("Fork failed");
            fprintf(stderr, "Fork failed for TA %d\n", i + 1);
            fflush(stdout);
            shmdt(shared_memory);
            shmctl(shmid, IPC_RMID, NULL);
            semctl(semid, 0, IPC_RMID);
            exit(1);
        }
    }

    // Parent process - wait for all TAs to finish
    int completed_TAs = 0;
    for (int i = 0; i < number_TAs; i++)
    {
        int status;
        waitpid(pids[i], &status, 0);
        completed_TAs++;
        printf("TA %d finished - Progress: %d/%d\n", i + 1, completed_TAs, number_TAs);
        fflush(stdout);
    }

    // Clean shared memory
    shmdt(shared_memory);
    shmctl(shmid, IPC_RMID, NULL);

    // clean up semaphores
    semctl(semid, 0, IPC_RMID);

    printf("Shared memory cleaned up\n");
    printf("TAs have finished everything and now they can exit");

    return 0;
}