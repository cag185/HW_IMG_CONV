/*------------------------------------------------------------------------------
--                                                                            --
--       .oooooo..o ooooo   ooooo ooooooooo.   oooooooooooo   .oooooo.        --
--      d8P'    `Y8 `888'   `888' `888   `Y88. `888'     `8  d8P'  `Y8b       --
--      Y88bo.       888     888   888   .d88'  888         888               --
--       `"Y8888o.   888ooooo888   888ooo88P'   888oooo8    888               --
--           `"Y88b  888     888   888`88b.     888    "    888               --
--      oo     .d8P  888     888   888  `88b.   888       o `88b    ooo       --
--      8""88888P'  o888o   o888o o888o  o888o o888ooooood8  `Y8bood8P'       --
--                                                                            --
--------------------------------------------------------------------------------
-- Vivado HLS 2D Convolutional Accelerator          author: Sebastian Sabogal --
--------------------------------------------------------------------------------
--                                                                            --
-- Copyright (C) 2020 SHREC.                                                  --
--                                                                            --
-- This file is part of HLS-2D-CONV.                                          --
--                                                                            --
-- HLS-2D-CONV is free software; you can redistribute it and/or modify        --
-- it under the terms of the GNU General Public License as published by the   --
-- Free Software Foundation; either version 3, or (at your option) any later  --
-- version.                                                                   --
-- HLS-2D-CONV is distributed in the hope that it will be useful, but         --
-- WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY --
-- or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License   --
-- for more details.                                                          --
-- You should have received a copy of the GNU General Public License along    --
-- with HLS-2D-CONV; see the file LICENSE.  If not see                        --
-- <http://www.gnu.org/licenses/>.                                            --
--                                                                            --
------------------------------------------------------------------------------*/

#include "convolution.hpp"

// kernel to be used for convolution
int8_t kern[K * K] = {
	1, 1, 1,
	1, -8, 1,
	1, 1, 1};
uint8_t shift_div = 0;

// software convolution function
void sw_conv(uint8_t *A, uint8_t *B)
{
	// A is the input picture and B is the output picture.
	// Note, these two arrays are 1D arrays, arranged row after row.

	// TODO

	// write the implementation of the software convolution function.
	// Couple of Hints:
	// 	1. figure out the limit of the loops that would scan the kernel over the image.
	//	2. have a variable of type int32_t to be used as the result of the convolution process. make sure to clear it before each convolution step.
	//	3. figure out the limit of the loops that would do the convolution (i.e. multiply th kernel with the corresponding pixels.
	//	4. in those loops, figure out the correct indexing method to access A (remember that A is a 1D image)
	// 	5. when you are done calculating the result, shift it to the right by the value shift_div defined above.
	//	6. before assigning the result to the corresponding pixel in B. make sure to check for saturation as follows:
	//		if the result > 0xFF then it should be clamped to 0xFF
	//		if less than 0, then it should be clamped to 0
	//		otherwise, it should be the same value.

	// 1. The kernel will move as length_input_image - [2 * floor(kernel_length/2)]

	// 	limit for the kernel to move
	int kernel_limit = IMG_WIDTH - (2 * floor(K / 2));
	printf("kernel limit: %d", kernel_limit);

	// 2.
	int32_t convolution_result = 0;

	// 3. The limits of the convolution procedure
	// will need to sum the product of each index of the kernel with its corresponding place in the image matrix
	// 	limit will be the num of the indices in the kernel
	int conv_limit = K^2;

	//4. to access an index of a pointer, we would use *(A + n)
	// at each new row of the image, the column index will reset to 1
	// to access  row r and column c, will need to access (r * ImageWidth) + c

	// 5. when done with calculating the result, will need to shift right by shift_div
	// IS THIS A LOGICAL SHIFT RIGHT OR SOMETHING TO DO WITH SHIFTING THE VALUES IN THE DATA STRUCTURE?

	// 6. before finalizing the result in b, make sure to clamp the values as needed

	// moves kernel along rows
	for (int r = 0; r < kernel_limit; r++)
	{
		// moves kernel along columns
		for (int c = 0; c < kernel_limit; c++)
		{
			// clear the result
			convolution_result = 0;

			// loop for convolution
			for (int con_r = 0; con_r < K; con_r++)
			{
				for (int con_c = 0; con_c < K; con_c++)
					{
						int kern_ind = 	con_r * K + con_c;
//						printf("Kern_IND: %d\n", kern_ind);
						// find the address of the row based on r
						int row = IMG_HEIGHT * r + con_r * IMG_HEIGHT;
						int col =  c + con_c;
						convolution_result += *(A + (row + col)) * kern[kern_ind];
//						printf("Convolution_result: %d\n", convolution_result);
						// TEST
//						convolution_result += *(A + (row + col));

					}
			}

			// check for clamping
			if(convolution_result >= 0xFF)
			{
				convolution_result = 0xFF;
			}
			else if(convolution_result < 0)
			{
				convolution_result = 0;
			}

			// store in B
			// r and c need to have some offset (limit of the kernel)
			int offset = floor(K/2);
			*(B + ((r + offset) * IMG_WIDTH + c + offset)) = convolution_result; // >> shift_div;
		}
	}

}

// SHIFT REGISTER HW CONVOLUTION SOLUTION
void hw_conv(stream_t &sin, stream_t &sout)
{

	// directives to assign ports
#pragma HLS INTERFACE ap_ctrl_none port = return
#pragma HLS INTERFACE axis port = sin
#pragma HLS INTERFACE axis port = sout

	uint8_t kbuf[K][K];					// the buffer used pixels to be multiplied by the kernel
	uint8_t lbuf[K - 1][IMG_WIDTH - K]; // the line buffer used for pixels in between the pixels multiplied by the kernel. (see lecture slides)

	// directives to partition these arrays
#pragma HLS ARRAY_PARTITION variable = kbuf complete dim = 0
#pragma HLS ARRAY_PARTITION variable = lbuf complete dim = 1

	int32_t result; // variable to store the conv result
	int lin_buff_length = IMG_WIDTH - K;	// variable to store the length of the line buffer to be used in loops
	int kern_length = K * K;
//	beat_t beat1; // variable to hold a new beat

	// a pipelined loop to go through all stream length + a delay (till the first convoluted pixel is correct.)
	for (int i = 0; i < STREAM_LENGTH + DELAY; ++i)
	{
		// set beat_1 to 0
//		beat1 = 0;
		result = 0;

		// pipeline directive
#pragma HLS PIPELINE II = 1

		/* Sliding Window */
		{
			// TODO

			// write code to shift pixels through first set (0 .. K-2) of kernel/line buffers
			// Hints:
			//	1. make sure to unroll all the loops written in this part to speed things up. use the command "# pragma HLS UNROLL"
			//	2. kbuf and ibuf can be index as a normal 2D array.

			// write code to shift pixels through last (K-1) kernel buffer
			// Hints:
			//	1. make sure to unroll all the loops written in this part to speed things up. use the command "# pragma HLS UNROLL"

			// write code to insert pixel into last pixel of K-1 kernel buffer
			// Hints:
			//	1. make sure that you only read in a new beat_t from the input stream as long as i < STREAM_LENGTH
			//	2. define a beat_t variable.
			//	3. use sin >> (your defined variable) to read in a beat from the input stream
			//	4. assign the value of .data(7,0) member function of your beat_t variable to the last pixel of K-1 kernel buffer



			// loop to move the image through the K-2 rows of K-buffers and L-buffers
			for (int r = 0; r < K-1; r++)
			{
				# pragma HLS UNROLL
				//loop through the kbuff
				for (int ki = 0; ki < K-1; ki++)
				{
					# pragma HLS UNROLL
					//the current index will take the value from the next index
					kbuf[r][ki] = kbuf[r][ki + 1];
				}
				// after that, the K-1st index of kbuf will take its value from line buffer
				kbuf[r][K-1] = lbuf[r][0];

				// loop through lbuff
				for (int li = 0; li < lin_buff_length-1; li++)
				{
					# pragma HLS UNROLL
					lbuf[r][li] = lbuf[r][li + 1];
				}
				//after, take the last index of the line buffer from the first index of the next row's kbuf
				lbuf[r][lin_buff_length-1] = kbuf[r+1][0];
			}

			//loop for the last kbuf row
			for(int kbi = 0; kbi < K-1; kbi++)
			{
				# pragma HLS UNROLL
				kbuf[K-1][kbi] = kbuf[K-1][kbi + 1];
			}

			//logic to fill the last kbuf (no more line buffs)
			kbuf[K-1][K-1] = 0;
			if (i < STREAM_LENGTH)
			{
				beat_t beat1;
				sin >> beat1;
				// last index of the last row of kbuf
				kbuf[K-1][K-1] = beat1.data(7,0);
			}
		}// end sliding window

		/* Convolution */
		{
			// TODO
			result = 0;
			// write code to implement the convolution operation.
			// Hints:
			//	1. reset the variable result before each conv operation.
			//	2. write loops to do the multiplication and accumulation in the result variable. use the command "# pragma HLS UNROLL"
			//	3. in those loops, figure out the correct indexing method to kernel kern (remember that kern is a 1D image)
			//	4. when you are done calculating the result, shift it to the right by the value shift_div defined above.
			//	5. make sure to check for saturation in the result variable as follows:
			//		if the result > 0xFF then it should be clamped to 0xFF
			//		if less than 0, then it should be clamped to 0
			//		otherwise, it should be the same value.

			// generate a beat_t object with the convoluted pixel and sending it the output stream
			// this is only done after a delay to ensure that we have calculated the correct pixel at the beginning
			for (int a = 0; a < kern_length; a++)
			{
				# pragma HLS UNROLL
				// result += part of kernel * part of image A
				// Kernel is 1D, array image is 2D
				int row = a/K;
				int col = a%K;
				result = result + (((kbuf[row][col]) * kern[a]));
			}

			// make sure to check the result for saturation
			// check for clamping
			if(result > 0xFF)
			{
				result = 0xFF;
			}
			else if(result < 0)
			{
				result = 0;
			}

			if (i >= DELAY)
			{
				beat_t val;
				val.data(7, 0) = result;
				val.keep(0, 0) = 0x1;
				val.last.set_bit(0, i == STREAM_LENGTH + DELAY - 1);
				sout << val;
			}
		}
	}
}

//// RING BUFFER HW CONVOLUTION SOLUTION
//void hw_conv(stream_t &sin, stream_t &sout)
//{
//
//	// directives to assign ports
//#pragma HLS INTERFACE ap_ctrl_none port = return
//#pragma HLS INTERFACE axis port = sin
//#pragma HLS INTERFACE axis port = sout
//
//	uint8_t kbuf[K][K];					// the buffer used pixels to be multiplied by the kernel
//	uint8_t lbuf[K - 1][IMG_WIDTH - K]; // the line buffer used for pixels in between the pixels multiplied by the kernel. (see lecture slides)
//
//	// directives to partition these arrays
//#pragma HLS ARRAY_PARTITION variable = kbuf complete dim = 0
//#pragma HLS ARRAY_PARTITION variable = lbuf complete dim = 1
//
//	int32_t result; // variable to store the conv result
//	int lin_buff_length = IMG_WIDTH - K;	// variable to store the length of the line buffer to be used in loops
//	int kern_length = K * K;
//	// Create a start and stop index for tracking the line (ring buffers) buffers
//	int start_pos = 0;
//	int end_pos = 0;
//
////	beat_t beat1; // variable to hold a new beat
//
//	// a pipelined loop to go through all stream length + a delay (till the first convoluted pixel is correct.)
//	for (int i = 0; i < STREAM_LENGTH + DELAY; ++i)
//	{
//		// set beat_1 to 0
////		beat1 = 0;
//		result = 0;
//
//		// pipeline directive
//#pragma HLS PIPELINE II = 1
//
//		/* Sliding Window */
//		{
//			// TODO
//
//			// write code to shift pixels through first set (0 .. K-2) of kernel/line buffers
//			// Hints:
//			//	1. make sure to unroll all the loops written in this part to speed things up. use the command "# pragma HLS UNROLL"
//			//	2. kbuf and ibuf can be index as a normal 2D array.
//
//			// write code to shift pixels through last (K-1) kernel buffer
//			// Hints:
//			//	1. make sure to unroll all the loops written in this part to speed things up. use the command "# pragma HLS UNROLL"
//
//			// write code to insert pixel into last pixel of K-1 kernel buffer
//			// Hints:
//			//	1. make sure that you only read in a new beat_t from the input stream as long as i < STREAM_LENGTH
//			//	2. define a beat_t variable.
//			//	3. use sin >> (your defined variable) to read in a beat from the input stream
//			//	4. assign the value of .data(7,0) member function of your beat_t variable to the last pixel of K-1 kernel buffer
//
//
//
//			// loop to move the image through the K-2 rows of K-buffers and L-buffers
//			for (int r = 0; r < K-1; r++)
//			{
//				# pragma HLS UNROLL
//				//loop through the kbuff
//
//				// set the pos vars to 0
////				start_pos = 0;
////				end_pos = 0;
//
//				for (int ki = 0; ki < K-1; ki++)
//				{
//					# pragma HLS UNROLL
//					//the current index will take the value from the next index
//					kbuf[r][ki] = kbuf[r][ki + 1];
//				}
//
//				// after that, the K-1st index of kbuf will take its value from line buffer
//				kbuf[r][K-1] = lbuf[r][start_pos++] ; // TODO // RETRIEVING VALUE
//				start_pos %= lin_buff_length; // need to move the start_pos after retrieving a value
//
//				//after, take the last index of the line buffer from the first index of the next row's kbuf
//				lbuf[r][end_pos++] = kbuf[r+1][0]; // TODO // STORING VALUE
//				end_pos %= lin_buff_length;	// move the end pointer
//			}
//
//			//loop for the last kbuf row
//			for(int kbi = 0; kbi < K-1; kbi++)
//			{
//				# pragma HLS UNROLL
//				kbuf[K-1][kbi] = kbuf[K-1][kbi + 1];
//			}
//
//			//logic to fill the last kbuf (no more line buffs)
//			kbuf[K-1][K-1] = 0;
//			if (i < STREAM_LENGTH)
//			{
//				beat_t beat1;
//				sin >> beat1;
//				// last index of the last row of kbuf
//				kbuf[K-1][K-1] = beat1.data(7,0);
//			}
//		}// end sliding window
//
//		/* Convolution */
//		{
//			// TODO
//			result = 0;
//			// write code to implement the convolution operation.
//			// Hints:
//			//	1. reset the variable result before each conv operation.
//			//	2. write loops to do the multiplication and accumulation in the result variable. use the command "# pragma HLS UNROLL"
//			//	3. in those loops, figure out the correct indexing method to kernel kern (remember that kern is a 1D image)
//			//	4. when you are done calculating the result, shift it to the right by the value shift_div defined above.
//			//	5. make sure to check for saturation in the result variable as follows:
//			//		if the result > 0xFF then it should be clamped to 0xFF
//			//		if less than 0, then it should be clamped to 0
//			//		otherwise, it should be the same value.
//
//			// generate a beat_t object with the convoluted pixel and sending it the output stream
//			// this is only done after a delay to ensure that we have calculated the correct pixel at the beginning
//			for (int a = 0; a < kern_length; a++)
//			{
//				# pragma HLS UNROLL
//				// result += part of kernel * part of image A
//				// Kernel is 1D, array image is 2D
//				int row = a/K;
//				int col = a%K;
//				result = result + (((kbuf[row][col]) * kern[a]));
//			}
//
//			// make sure to check the result for saturation
//			// check for clamping
//			if(result > 0xFF)
//			{
//				result = 0xFF;
//			}
//			else if(result < 0)
//			{
//				result = 0;
//			}
//
//			if (i >= DELAY)
//			{
//				beat_t val;
//				val.data(7, 0) = result;
//				val.keep(0, 0) = 0x1;
//				val.last.set_bit(0, i == STREAM_LENGTH + DELAY - 1);
//				sout << val;
//			}
//		}
//	}
//}
