// Copyright (C) 2017 Pierre Parent <pierre.parent ''at=- pparent.fr>
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

#include <module.h>
#include <dryos.h>
#include <bmp.h>
#include <config.h>
#include <menu.h>
#include <math.h>
#include <lens.h>
#include <imath.h>

static int video_av_running;



// configs
static CONFIG_INT("movie.av_expo.enabled", av_expo_enabled, 0);
static CONFIG_INT("movie.av_expo.av_value", av_value, 30);
static CONFIG_INT("movie.av_expo.smooth_changes", smooth_changes, 1);
static CONFIG_INT("movie.av_expo.allow_jumps", allow_jumps, 1);

extern void set_movie_digital_iso_gain_for_gradual_expo(int gain);
extern int prop_set_rawiso_approx(unsigned iso);

void FAST digital_gain_simulate_virtual_iso (int current_virtual_iso,int current_frame_iso)
{
            if (smooth_changes && current_virtual_iso!=current_frame_iso)
            {
            set_movie_digital_iso_gain_for_gradual_expo(1024.0f * powf(2, -1.0f*(current_frame_iso-current_virtual_iso)/8.0f));
            }
            else
              set_movie_digital_iso_gain_for_gradual_expo(1024.0f);  
}

int tv_step(int *current_tv,int desired_tv)
{
         
        if ((*current_tv)<0x60 || (*current_tv)>0x98)
             return 0;
             
         int diff=(*current_tv)-(int)COERCE(desired_tv,0x60, 0x98);
         
         //we don't need to change tv
         if (diff==0)
         {
                return 0;
         }
         
         //If we could not achieve a 4 step then do a 1 step

         if(diff<0)
            (*current_tv)=(*current_tv)+1;
         if(diff>0   )
            (*current_tv)=(*current_tv)-1;
         
         bv_set_rawshutter((*current_tv));
         
         return 1;
                 
}

void iso_step(int *current_iso,int *last_requested_iso,int desired_iso)
{
        unsigned analog_iso,digital_gain,value_to_set;

        
        if (desired_iso !=0)
        {
            //Increase or decrease virtual iso in the desired direction
            if((*current_iso)-desired_iso <0 )
                        (*current_iso)++;

            if((*current_iso)-desired_iso >0 )
                        (*current_iso)--;
        }
                 
          //The value we should try to set physically to the sensor
          //Is our virtual iso, plus compensation for shutter.
            value_to_set=(*current_iso);
          
          value_to_set=COERCE(value_to_set,MIN_ISO,MAX_ISO);
          
          //If the value to set is a full analog ISO, then request to set it
          split_iso(value_to_set, &analog_iso, &digital_gain);
           if (digital_gain==0 || smooth_changes==0  )
           {
                bv_set_rawiso(value_to_set);
                (*last_requested_iso)=value_to_set;
           }
}

int  virtual_expo_step(int *current_virtual_expo,int desired_expo,int current_hard_expo)
{
    int diff1=(*current_virtual_expo)-desired_expo;
    int diff2=(*current_virtual_expo)-current_hard_expo;
    
    if (diff1<0 && diff2<8)
        {(*current_virtual_expo)++; return 0;}
    if (diff1>0 && diff2>-8)
        {(*current_virtual_expo)--; return 0;}
        
//     if (diff1<0 && diff2<8  && rand()%4==0)
//         {(*current_virtual_expo)++; return 0;}
//     if (diff1>0 && diff2>-8  && rand()%4==0 )
//         {(*current_virtual_expo)--; return 0;}   
        
    if ( diff2>10)
         {(*current_virtual_expo)--; return 0;}
         
    if ( diff2<-10)
         {(*current_virtual_expo)++; return 0;}       
         
        return 1;
}

#define AE_SPEED 32
#define JUMP_EXPO_TRIGGER 30
#define JUMP_EXPO_STOP 2

void compute_jumps(int *current_expo,int desired_expo, int *current_tv, int *current_iso, int* last_requested_iso, int* current_hard_expo)
{
 int direction=0;
 if (ABS((desired_expo)-(*current_expo))<JUMP_EXPO_TRIGGER )
     return;
  direction=((desired_expo)-(*current_expo))/ABS((desired_expo)-(*current_expo));
 
 //Adjust Tv in the right direction
 while (ABS((*current_expo)-(desired_expo))>=JUMP_EXPO_STOP &&
        (*current_tv)>0x60 && (*current_tv)<0x98)
        {
            (*current_tv)-=direction;
            (*current_expo)+=direction;
        }
        
 //Adjust ISO in the right direction        
 while (ABS((*current_expo)-(desired_expo))>=JUMP_EXPO_STOP &&
        (*current_iso)>MIN_ISO && (*current_iso)<104)
        {
            (*current_iso)+=direction;
            (*current_expo)+=direction;
        }      
        
  bv_set_rawshutter((*current_tv));
 (*last_requested_iso)=((((*current_iso)+4)/8)*8);
 COERCE((*last_requested_iso),MIN_ISO,104);
 bv_set_rawiso((*last_requested_iso));
 while (read_frame_iso()!=(*last_requested_iso))
 {bv_set_rawiso((*last_requested_iso));}
 (*current_hard_expo)=(*last_requested_iso)-(*current_tv);
 digital_gain_simulate_virtual_iso((*current_expo),(*current_hard_expo));  
}

static void FAST video_av_task()
{
    int current_iso,current_frame_iso,last_requested_iso,desired_iso,virtual_iso_at_last_request;
    int current_tv, desired_tv, shutter_iso_compensation=0;
    video_av_running = 1;
    
    int t0 = get_ms_clock_value();
    int t1=t0;
    int t2=get_ms_clock_value(),t3=t2;
    //Init values       
    desired_iso=lens_info.raw_iso_ae; 
    current_frame_iso=read_frame_iso();last_requested_iso=0;current_iso=lens_info.raw_iso;
    desired_tv=lens_info.raw_shutter_ae;current_tv=desired_tv;
    virtual_iso_at_last_request=lens_info.raw_iso;
    unsigned int kk=0;
    int last_frame_iso;
    int frame_tv,current_hard_tv;
    int current_hard_expo,current_virtual_expo,desired_expo;
    
    current_hard_expo=lens_info.raw_iso-lens_info.raw_shutter;
    current_virtual_expo=lens_info.raw_iso-lens_info.raw_shutter;
    
    TASK_LOOP
    {
        if (  !gui_menu_shown() && av_expo_enabled && expo_override_active() &&  shooting_mode == SHOOTMODE_MOVIE )
        {
        //Start of main loop in activated mode    
            
        current_frame_iso=read_frame_iso();                
        if( current_frame_iso == last_requested_iso) digital_gain_simulate_virtual_iso(current_virtual_expo,last_requested_iso-current_tv);
        
        //If last requested values have been applied to sensor
        if( current_frame_iso == last_requested_iso || current_frame_iso == 0 || smooth_changes==0 )
            {
            digital_gain_simulate_virtual_iso(current_virtual_expo,last_requested_iso-current_tv);    
            current_hard_expo=last_requested_iso-current_tv;
            current_hard_tv=current_tv;

            last_frame_iso=current_frame_iso;
      
            //Update desired values given by canon AE
            desired_iso=(int)(lens_info.raw_iso_ae);
            desired_tv=(int)COERCE(lens_info.raw_shutter_ae,0x60, 0x98);
            desired_expo=desired_iso-desired_tv;
            
            //Update Av value if needed
            av_value=COERCE(av_value,lens_info.raw_aperture_min,lens_info.raw_aperture_max);
            if(av_value!=lens_info.raw_aperture)
                lens_set_rawaperture(av_value);
            
            
            if (current_iso==0)
                current_iso=(int)(lens_info.raw_iso);
            
            //We can rest and wait because there won't be any value change
            //In FRAME_ISO which could provoke glitch.
            msleep(AE_SPEED);
            }

                
            
            
           t0 = get_ms_clock_value();  
        if (t0-t1>=AE_SPEED)
        {
      
            //Update desired values given by canon AE
            if (desired_iso==0)
            {
            desired_iso=(int)(lens_info.raw_iso_ae);
            desired_tv=(int)COERCE(lens_info.raw_shutter_ae,0x60, 0x98);
            desired_expo=desired_iso-desired_tv;
            }
            kk++;

                //Jumps
                if (allow_jumps)
                    compute_jumps(&current_virtual_expo,desired_expo, &current_tv, &current_iso, &last_requested_iso, &current_hard_expo);
                
                //iso_step
                if(last_requested_iso==last_frame_iso|| ABS(last_requested_iso-current_iso)<7)
                    iso_step(&current_iso,&last_requested_iso,desired_iso);
                
                //tv_step
                if ( last_requested_iso!=last_frame_iso || kk%12==0 || !smooth_changes )
                {
                    int oldtv=current_tv;
                    tv_step(&current_tv,desired_tv);
                    bv_set_rawshutter(current_tv);
                    if(last_requested_iso==last_frame_iso)
                        current_virtual_expo-=(current_tv-oldtv);
                }
                
                //virtual expo step for smoothing
                if (last_requested_iso!=last_frame_iso || current_tv==desired_tv)
                    virtual_expo_step(&current_virtual_expo,desired_expo,current_hard_expo);
                
 
            t1=t0;
            
        }   
            digital_gain_simulate_virtual_iso(current_virtual_expo,current_hard_expo); 
        
        //End of main loop in activated mode    
        }
        else
        {
            msleep(500);
            current_iso=lens_info.raw_iso;
            last_requested_iso=lens_info.raw_iso;
            current_frame_iso=lens_info.raw_iso;
            current_tv=lens_info.raw_shutter;
            shutter_iso_compensation=0;
            virtual_iso_at_last_request=lens_info.raw_iso;
            current_hard_expo=lens_info.raw_iso-lens_info.raw_shutter;
            current_virtual_expo=lens_info.raw_iso-lens_info.raw_shutter;
            digital_gain_simulate_virtual_iso(current_virtual_expo,current_hard_expo);
        }
    }
    
    goto quit;
quit:
    video_av_running = 0;
}



static unsigned int video_av_shoot_task(){
    if(av_expo_enabled &&
        shooting_mode == SHOOTMODE_MOVIE &&
        !video_av_running
    )
        task_create("video_av", 0x1c, 0x1000, video_av_task, (void*)0);

    return 0;
}


static MENU_UPDATE_FUNC(menu_custom_display_av) {
    int av_disp=values_aperture[raw2index_aperture(av_value)];
    MENU_SET_VALUE("%d.%d", av_disp/10,av_disp%10);
}


static struct menu_entry video_av_menu[] =
{
    {
         .name = "Av exposure",
         .priv = &av_expo_enabled,
         .max = 1,
        .help = "Av semi-automatic exposure for video.",
        .help2 = "Exposure overide must be set.",
        .children = (struct menu_entry[])
        {
            {
                .name = "Av",
                .priv = &av_value,
                .update = menu_custom_display_av,
                .help = "Desired aperture value.",
                .max = 32,
                .min = 22,
            },
            {
                .name = "Smooth changes",
                .help = "Smooth expo changes.",
                .priv = &smooth_changes,
                .max = 1
            },
            {
                .name = "Allow jumps",
                .help = "Allow expo jumps when big changes in light happen.",
                .priv = &allow_jumps,
                .max = 1
            },            
            MENU_EOL,
        }
    }
};

static unsigned int video_av_init() {
    video_av_menu->children[0].max=lens_info.raw_aperture_max;
    video_av_menu->children[0].min=lens_info.raw_aperture_min;
    av_value=COERCE(av_value,lens_info.raw_aperture_min,lens_info.raw_aperture_max);
    menu_add("Movie", video_av_menu, COUNT(video_av_menu));
    return 0;
}

static unsigned int video_av_deinit() {
        return 0;
}

MODULE_INFO_START()
    MODULE_INIT(video_av_init)
    MODULE_DEINIT(video_av_deinit)
MODULE_INFO_END()

MODULE_CBRS_START()
    MODULE_CBR(CBR_SHOOT_TASK, video_av_shoot_task, 0)
MODULE_CBRS_END()

MODULE_CONFIGS_START()
      MODULE_CONFIG(av_expo_enabled)
      MODULE_CONFIG(av_value)
      MODULE_CONFIG(smooth_changes)    
      MODULE_CONFIG(allow_jumps)        
MODULE_CONFIGS_END()

//             bmp_printf(FONT_MED,300,300,"TV XXXXXXXXXXXXX");
//              bmp_printf(FONT_MED,300,300,"TV %d %d",current_tv,desired_tv);
//              
//             bmp_printf(FONT_MED,50,50,"ISO XXXXXXXXXXXXX");
//              bmp_printf(FONT_MED,50,50,"ISO %d %d %d %d",current_frame_iso,current_iso,last_requested_iso,desired_iso);
//              
//             bmp_printf(FONT_MED,150,150,"EXPO XXXXXXXXXXXXX");
//              bmp_printf(FONT_MED,150,150,"EXPO %d %d %d",current_virtual_expo,current_hard_expo,desired_expo);
