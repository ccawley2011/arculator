#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>

#include "82c711_fdc.h"
#include "arc.h"
#include "arm.h"
#include "config.h"
#include "ddnoise.h"
#include "disc.h"
#include "disc_adf.h"
#include "disc_apd.h"
#include "disc_fdi.h"
#include "disc_jfd.h"
#include "disc_ssd.h"
#include "ics.h"
#include "ioc.h"
#include "keyboard.h"
#include "mem.h"
#include "memc.h"
#include "plat_input.h"
#include "plat_video.h"
#include "podules.h"
#include "sound.h"
#include "soundopenal.h"
#include "vidc.h"
#include "video_sdl2.h"
#include "wd1770.h"

#include "hostfs.h"

/*0=Arthur
  1=RiscOS 2
  2=RiscOS 3.1 with WD1772
  3=RiscOS 3.1 with 82c711
  4=MAME 'ertictac' set
  5=MAME 'poizone' set
  There are two RiscOS 3.1 sets as configuring for 82c711 corrupts ADFS CMOS space
  used for WD1772 - the effect is that WD1772 will hang more often if they are the
  same set.*/
int romset=2;
int romsavailable[6];

void fdiclose();
int firstfull=1;
int memsize=4096;
static float inssecf;  /*Millions of instructions executed in the last second*/
int inssec;            /*Speed ratio percentage (100% = realtime emulation), updated by updateins()*/
int updatemips;        /*1 if MIPS counter has not been updated since last updateins() call*/
static int frameco=0;  /*Number of 1/100 second executions (arm_run() calls) since last updateins()*/
char exname[512];

int jint,jtotal;

void updateins()
{
        inssecf=(float)inscount/1000000;
        inscount=0;
        inssec=frameco;
        frameco=0;
        jtotal=jint;
        jint=0;
        updatemips=1;
}

FILE *rlog = NULL;
void rpclog(const char *format, ...)
{
#ifdef DEBUG_LOG
   char buf[1024];

   if (!rlog)
   {
           rlog=fopen("arclog.txt","wt");
           if (!rlog)
           {
                   perror("fopen");
                   exit(-1);
           }
   }

   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);

   fprintf(stderr, "%s", buf);

   fputs(buf,rlog);
   fflush(rlog);
#endif
}

void fatal(const char *format, ...)
{
   char buf[1024];

   if (!rlog) rlog=fopen("arclog.txt","wt");

   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);
   fputs(buf,rlog);
   fflush(rlog);

   fprintf(stderr, "%s", buf);

   dumpregs();
   exit(-1);
}

#ifndef WIN32
void error(const char *format, ...)
{
   char buf[1024];

   if (!rlog) rlog=fopen("arclog.txt","wt");

   va_list ap;
   va_start(ap, format);
   vsprintf(buf, format, ap);
   va_end(ap);
   fputs(buf,rlog);
   fflush(rlog);

   fprintf(stderr, "%s", buf);
}
#endif // !WIN32

int limitspeed;

/*Preference order : ROS3, ROS2, Arthur, Poizone, Erotactic/Tictac
  ROS3 with WD1772 is not considered, if ROS3 is available run with 82c711 as
  this is of more use to most users*/
int rompreffered[5]={3,1,0,5,4};

/*Establish which ROMs are available*/
void establishromavailability()
{
        int d=0;
        int c=romset;
        for (romset=0;romset<7;romset++)
        {
                romsavailable[romset]=!loadrom();
//                rpclog("romset %i %s\n",romset,(romsavailable[romset])?"available":"not available");
        }
        romset=c;
        for (c=0;c<6;c++)
            d|=romsavailable[c];
        if (!d)
        {
                error("No ROMs are present!");
                exit(-1);
        }
        if (romsavailable[romset])
        {
                loadrom();
                return;
        }
        for (c=0;c<5;c++)
        {
                romset=rompreffered[c];
                if (!loadrom())
                   return;
        }
        error("No ROM sets available!");
        exit(-1);
}

void arc_set_cpu(int cpu, int memc);

void arc_init()
{
        char *p;
        char s[512];
        int c;
        al_init_main(0, NULL);

#ifdef WIN32
        get_executable_name(exname,511);
        p = (char *)get_filename(exname);
        *p = 0;
#endif

        loadconfig();
        
        initvid();

#if 0
        initarculfs();
#endif
        hostfs_init();
        resetide();
        p = (char *)config_get_string(NULL,"mem_size",NULL);
        if (!p || !strcmp(p,"4096")) memsize=4096;
        else if (!strcmp(p,"8192"))  memsize=8192;
        else if (!strcmp(p,"2048"))  memsize=2048;
        else if (!strcmp(p,"1024"))  memsize=1024;
        else if (!strcmp(p,"512"))   memsize=512;
        else                         memsize=16384;
        initmem(memsize);
        
        p = (char *)config_get_string(NULL,"rom_set",NULL);
        if (!p || !strcmp(p,"3")) romset=3;
        else if (!strcmp(p,"1"))  romset=1;
        else if (!strcmp(p,"2"))  romset=2;
        else if (!strcmp(p,"4"))  romset=4;
        else if (!strcmp(p,"5"))  romset=5;
        else if (!strcmp(p,"6"))  romset=6;
        else                      romset=0;
        establishromavailability();

        resizemem(memsize);
        
        initmemc();
        resetarm();
        loadcmos();
        ioc_reset();
        keyboard_init();
        resetmouse();

        fullscreen=0;
        //mousehack=0;
        limitspeed=1;
        reinitvideo();
        if (soundena) 
           al_init();
        initjoy();


        disc_init();
        adf_init();
        apd_init();
        fdi_init();
        jfd_init();
        ssd_init();
        ddnoise_init();

        wd1770_reset();
        c82c711_fdc_reset();

        for (c=0;c<4;c++)
        {
                sprintf(s,"disc_name_%i",c);
                p = (char *)config_get_string(NULL,s,NULL);
                if (p) {
                   disc_close(c);
                   strcpy(discname[c], p);
                   disc_load(c, discname[c]);
                   ioc_discchange(c);
                }
        }
        if (romset==3) fdctype=1;
        else	       fdctype=0;

        arc_set_cpu(arm_cpu_type, memc_type);

        resetst506();
        resetics();
        
        podules_reset();
        opendlls();
}

int speed_mhz;

void arc_reset()
{
        loadrom();
        loadcmos();
        resizemem(memsize);
        arc_set_cpu(arm_cpu_type, memc_type);
        resetarm();
        memset(ram,0,memsize*1024);
        resetmouse();
        ioc_reset();
        keyboard_init();
        wd1770_reset();
        c82c711_fdc_reset();
        resetst506();
        resetics();
        podules_reset();
}

void arc_setspeed(int mhz)
{
        rpclog("arc_setspeed : %i MHz\n", mhz);
//        ioc_recalctimers(mhz);
        speed_mhz = mhz;
        disc_poll_time = 2 * mhz;
        sound_poll_time = 4 * mhz;
        keyboard_poll_time = 10000 * mhz;
	memc_refresh_time = ((32 << 10) * speed_mhz) / 8;
        rpclog("memc_refresh_time=%i\n", memc_refresh_time);
}

static struct
{
        char name[50];
        int mem_speed;
        int is_memc1;
} arc_memcs[] =
{
        {"MEMC1",             8, 1},
        {"MEMC1A at 8 MHz",   8, 0},
        {"MEMC1A at 12 MHz", 12, 0},
        {"MEMC1A at 16 MHz", 16, 0}
};

static struct
{
        char name[50];
        int cpu_speed;
        int has_swp;
        int has_cp15;
} arc_cpus[] =
{
        {"ARM2",          0,  0, 0},
        {"ARM250",        0,  1, 0},
        {"ARM3 (20 MHz)", 20, 1, 1},
        {"ARM3 (25 MHz)", 25, 1, 1},
        {"ARM3 (26 MHz)", 26, 1, 1},
        {"ARM3 (30 MHz)", 30, 1, 1},
        {"ARM3 (33 MHz)", 33, 1, 1},
        {"ARM3 (35 MHz)", 35, 1, 1},
};

void arc_set_cpu(int cpu, int memc)
{
        rpclog("arc_setcpu : setting CPU to %s\n", arc_cpus[cpu].name);
        arm_mem_speed = arc_memcs[memc].mem_speed;
        memc_is_memc1 = arc_memcs[memc].is_memc1;
        rpclog("setting memc to %i %i %i\n", memc, memc_is_memc1, arm_mem_speed);
        if (arc_cpus[cpu].cpu_speed)
                arm_cpu_speed = arc_cpus[cpu].cpu_speed;
        else
                arm_cpu_speed = arm_mem_speed;
        arm_has_swp   = arc_cpus[cpu].has_swp;
        arm_has_cp15  = arc_cpus[cpu].has_cp15;
        arc_setspeed(arm_cpu_speed);
        mem_updatetimings();
}

static int ddnoise_frames = 0;
void arc_run()
{
        LOG_EVENT_LOOP("arc_run()\n");
        execarm((speed_mhz * 1000000) / 100);
        cmostick();
        polljoy();
        mouse_poll_host();
        keyboard_poll_host();
        if (mousehack) doosmouse();
        frameco++;
        ddnoise_frames++;
        if (ddnoise_frames == 10)
        {
                ddnoise_frames = 0;
                ddnoise_mix();
        }
        LOG_EVENT_LOOP("END arc_run()\n");
}

void arc_close()
{
//        output=1;
//        execarm(16000);
//        vidc_dumppal();
        dumpregs();
        savecmos();
        saveconfig();
        disc_close(0);
        disc_close(1);
        disc_close(2);
        disc_close(3);
        rpclog("ddnoise_close\n");
        ddnoise_close();
        rpclog("closevideo\n");
        closevideo();
        rpclog("arc_close done\n");
}

#ifndef WIN32
void updatewindowsize(int x, int y)
{
}

void sdl_enable_mouse_capture() {
        mouse_capture_enable();
        SDL_SetWindowGrab(sdl_main_window, SDL_TRUE);
        mousecapture = 1;
        updatemips = 1;
}

void sdl_disable_mouse_capture() {
        SDL_SetWindowGrab(sdl_main_window, SDL_FALSE);
        mouse_capture_disable();
        mousecapture = 0;
        updatemips = 1;
}

static int quited = 0;

int main(int argc, char *argv[])
{
        strncpy(exname, argv[0], 511);
        char *p = (char *)get_filename(exname);
        *p = 0;

        rpclog("Arculator startup\n");

        arc_init();

        if (!video_renderer_init(NULL))
        {
                fatal("Video renderer init failed");
        }
        input_init();

        struct timeval tp;
        time_t last_seconds = 0;

        while (!quited)
        {
                LOG_EVENT_LOOP("event loop\n");
                if (gettimeofday(&tp, NULL) == -1)
                {
                        perror("gettimeofday");
                        fatal("gettimeofday failed\n");
                }
                else if (!last_seconds)
                {
                        last_seconds = tp.tv_sec;
                        rpclog("start time = %d\n", last_seconds);
                }
                else if (last_seconds != tp.tv_sec)
                {
                        updateins();
                        last_seconds = tp.tv_sec;
                }
                SDL_Event e;
                while (SDL_PollEvent(&e) != 0)
                {
                        if (e.type == SDL_QUIT)
                        {
                                quited = 1;
                        }
                        if (e.type == SDL_MOUSEBUTTONUP)
                        {
                                if (!mousecapture)
                                {
                                        rpclog("Mouse click -- enabling mouse capture\n");
                                        sdl_enable_mouse_capture();
                                }
                        }
                        if (e.type == SDL_WINDOWEVENT)
                        {
                                switch (e.window.event)
                                {
                                        case SDL_WINDOWEVENT_FOCUS_LOST:
                                        if (mousecapture)
                                        {
                                                rpclog("Focus lost -- disabling mouse capture\n");
                                                sdl_disable_mouse_capture();
                                        }
                                        break;
                                
                                        default:
                                        break;
                                }
                        }
                        if ((key[KEY_LCONTROL] || key[KEY_RCONTROL])
                            && key[KEY_END]
                            && !fullscreen && mousecapture)
                        {
                                rpclog("CTRL-END pressed -- disabling mouse capture\n");
                                sdl_disable_mouse_capture();
                        }
                }

                // Run for 10 ms of processor time
                arc_run();

                // Sleep to make it up to 10 ms of real time
                static Uint32 last_timer_ticks = 0;
                static int timer_offset = 0;
                Uint32 current_timer_ticks = SDL_GetTicks();
                Uint32 ticks_since_last = current_timer_ticks - last_timer_ticks;
                last_timer_ticks = current_timer_ticks;
                timer_offset += 10 - (int)ticks_since_last;
                // rpclog("timer_offset now %d; %d ticks since last; delaying %d\n", timer_offset, ticks_since_last, 10 - ticks_since_last);
                if (timer_offset > 100 || timer_offset < -100)
                {
                        timer_offset = 0;
                }
                else if (timer_offset > 0)
                {
                        SDL_Delay(timer_offset);
                }
        }
        rpclog("SHUTTING DOWN\n");

        arc_close();

        input_close();
        video_renderer_close();
}
#endif
