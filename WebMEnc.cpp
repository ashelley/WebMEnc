// Copyright (c) 2011 Bo Zhou<Bo.Schwarzstein@gmail.com>
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//		http://www.apache.org/licenses/LICENSE-2.0 
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <vpx/vpx_codec.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_image.h>
#include <vpx/vpx_version.h>

#include <vpx/vp8cx.h>

#include "EbmlWriter.h"

#include <IL/il.h>
#include <IL/ilu.h>

#if defined(_MSC_VER)
/* MSVS doesn't define off_t, and uses _f{seek,tell}i64 */
#define fseeko _fseeki64
#define ftello _ftelli64
#endif

#define RGB2YUV_SHIFT 15
#define BY ( (int)(0.114*219/255*(1<<RGB2YUV_SHIFT)+0.5))
#define BV (-(int)(0.081*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define BU ( (int)(0.500*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define GY ( (int)(0.587*219/255*(1<<RGB2YUV_SHIFT)+0.5))
#define GV (-(int)(0.419*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define GU (-(int)(0.331*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define RY ( (int)(0.299*219/255*(1<<RGB2YUV_SHIFT)+0.5))
#define RV ( (int)(0.500*224/255*(1<<RGB2YUV_SHIFT)+0.5))
#define RU (-(int)(0.169*224/255*(1<<RGB2YUV_SHIFT)+0.5))

static inline void rgb24toyv12(vpx_image_t *rgbImage, vpx_image_t *yv12Image)
{
	unsigned int width = rgbImage->w;
	unsigned int height = rgbImage->h;
	unsigned int planeSize = width * height;

	unsigned char* rgb = rgbImage->img_data;

	unsigned char* yPlane = yv12Image->img_data;
	unsigned char* uPlane = yPlane + planeSize;
	unsigned char* vPlane = uPlane + (planeSize >> 2);

	unsigned int i;

	// Y pass.
	for (i = 0; i < width * height; ++ i)
	{
		unsigned int r = rgb[3*i+0];
		unsigned int g = rgb[3*i+1];
		unsigned int b = rgb[3*i+2];

		unsigned int y = ((RY*r + GY*g + BY*b)>>RGB2YUV_SHIFT) + 16;
		unsigned int u = ((RU*r + GU*g + BU*b)>>RGB2YUV_SHIFT) + 128;
		unsigned int v = ((RV*r + GV*g + BV*b)>>RGB2YUV_SHIFT) + 128;

		rgb[3*i+0] = y;
		rgb[3*i+1] = u;
		rgb[3*i+2] = v;

		yPlane[i] = y;
	}

	// UV pass, 4 x 4 downsampling.
	i = 0;
	for (unsigned int y = 0; y < height; y += 2)
	{
		for (unsigned int x = 0; x < width; x += 2)
		{
			unsigned int sumU = 0, sumV = 0;

			// Left Root.
			//
			sumU += rgb[3*(y * width + x)+1];
			sumV += rgb[3*(y * width + x)+2];

			// Right Root.
			sumU += rgb[3*(y * width + x + 1)+1];
			sumV += rgb[3*(y * width + x + 1)+2];

			// Left Top.
			sumU += rgb[3*((y+1) * width + x)+1];
			sumV += rgb[3*((y+1) * width + x)+2];

			// Right Top.
			sumU += rgb[3*((y+1) * width + x + 1)+1];
			sumV += rgb[3*((y+1) * width + x + 1)+2];

			// Get average.
			uPlane[i] = sumU / 4;
			vPlane[i] = sumV / 4;

			i += 1;
		}
	}
}

bool readImage(char *filename, int frameNumber, vpx_image_t **pRGBImage, vpx_image_t **pYV12Image, int flip)
{
	// Load image.
	//
	char path[512];
	sprintf(path, filename, frameNumber);

	ILuint imageHandle;
	ilGenImages(1, &imageHandle);
	ilBindImage(imageHandle);

	ILboolean ok = ilLoadImage(path);

	if (ok)
	{
		if (ilConvertImage(IL_RGB, IL_UNSIGNED_BYTE))
		{
			unsigned int w = ilGetInteger(IL_IMAGE_WIDTH);
			unsigned int h = ilGetInteger(IL_IMAGE_HEIGHT);

			if (*pRGBImage == NULL)
			{
				*pRGBImage = vpx_img_alloc(NULL, VPX_IMG_FMT_RGB24, w, h, 1);
			}

			if (*pYV12Image == NULL)
			{
				*pYV12Image = vpx_img_alloc(NULL, VPX_IMG_FMT_I420, w, h, 1);
			}

			memcpy((*pRGBImage)->img_data, ilGetData(), w * h * 3);

			rgb24toyv12(*pRGBImage, *pYV12Image);

			if (flip)
			{
				vpx_img_flip(*pYV12Image);
			}

			ilDeleteImages(1, &imageHandle);

			return true;
		}
	}
	else
	{
		ILenum ilError = ilGetError();
		fprintf(stderr, "Can't load [%s], [%x %s]\n.", path, ilError, iluErrorString(ilError));
	}

	return false;
}

int main(int argc, char* argv[])
{
	if (argc != 7)
	{
		fprintf(stderr, "  Usage: WebMEnc <input filename> <flip> <threads> <bit-rates> <frame-per-second> <output filename>\nExample: WebMEnc frame.%%.5d.tiff 1 8 512 30 frame.webm\n");
		return EXIT_FAILURE;
	}

	ilInit();
	iluInit();

	// Initialize VPX codec.
	//
	vpx_codec_ctx_t vpxContext;
	vpx_codec_enc_cfg_t vpxConfig;

    if (vpx_codec_enc_config_default(vpx_codec_vp8_cx(), &vpxConfig, 0) != VPX_CODEC_OK)
	{
        return EXIT_FAILURE;
    }

	// Try to load the first frame to initialize width and height.
	//
	int flip = (bool)atoi(argv[2]);

	vpx_image_t *rgbImage = NULL, *yv12Image = NULL;
	if (readImage(argv[1], 0, &rgbImage, &yv12Image, flip) == false)
	{
		return EXIT_FAILURE;
	}
	vpxConfig.g_h = yv12Image->h;
	vpxConfig.g_w = yv12Image->w;

	vpxConfig.g_threads = atoi(argv[3]);

	vpxConfig.rc_target_bitrate = atoi(argv[4]);

	vpxConfig.g_timebase.den = atoi(argv[5]);
	vpxConfig.g_timebase.num = 1;

	// Prepare the output .webm file.
	//
	EbmlGlobal ebml;
	memset(&ebml, 0, sizeof(EbmlGlobal));
	ebml.last_pts_ms = -1;
	ebml.stream = fopen(argv[6], "wb");
	if (ebml.stream == NULL)
	{
		return EXIT_FAILURE;
	}
	vpx_rational ebmlFPS = vpxConfig.g_timebase;
	struct vpx_rational arg_framerate = {atoi(argv[5]), 1};
	Ebml_WriteWebMFileHeader(&ebml, &vpxConfig, &arg_framerate);

	unsigned long duration = (float)arg_framerate.den / (float)arg_framerate.num * 1000;

	if (vpx_codec_enc_init(&vpxContext, vpx_codec_vp8_cx(), &vpxConfig, 0) != VPX_CODEC_OK)
	{
        return EXIT_FAILURE;
    }

	//
	fprintf(stdout, "input=%s\nflip=%s\nthreads=%s\nbps=%s\nfps=%s\noutput=%s\n", argv[1], argv[2], argv[3], argv[4], argv[5], argv[6]);

	
	// Reading image file sequence, encoding to .WebM file.
	//
	int frameNumber = 0;
	while(readImage(argv[1], frameNumber, &rgbImage, &yv12Image, flip))
	{
		vpx_codec_err_t vpxError = vpx_codec_encode(&vpxContext, yv12Image, frameNumber, duration, 0, 0);
		if (vpxError != VPX_CODEC_OK)
		{
			return EXIT_FAILURE;
		}
		
		vpx_codec_iter_t iter = NULL;
		const vpx_codec_cx_pkt_t *packet;
		while( (packet = vpx_codec_get_cx_data(&vpxContext, &iter)) )
		{
			Ebml_WriteWebMBlock(&ebml, &vpxConfig, packet);
		}

		frameNumber ++;
		printf("Processed %d frames.\r", frameNumber);

		vpx_img_free(yv12Image);
		yv12Image = NULL;
	}

	Ebml_WriteWebMFileFooter(&ebml, 0);
	fclose(ebml.stream);

	vpx_codec_destroy(&vpxContext);

	return EXIT_SUCCESS;
}
