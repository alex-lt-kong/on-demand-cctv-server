SRC_DIR = ./src
OPT = -Wall -O3
OCV_LIB = -lopencv_stitching -lopencv_alphamat -lopencv_aruco -lopencv_bgsegm -lopencv_bioinspired -lopencv_ccalib -lopencv_dnn_objdetect -lopencv_dnn_superres -lopencv_dpm -lopencv_face -lopencv_freetype -lopencv_fuzzy -lopencv_hdf -lopencv_hfs -lopencv_img_hash -lopencv_intensity_transform -lopencv_line_descriptor -lopencv_mcc -lopencv_quality -lopencv_rapid -lopencv_reg -lopencv_rgbd -lopencv_saliency -lopencv_shape -lopencv_stereo -lopencv_structured_light -lopencv_phase_unwrapping -lopencv_superres -lopencv_optflow -lopencv_surface_matching -lopencv_tracking -lopencv_highgui -lopencv_datasets -lopencv_text -lopencv_plot -lopencv_ml -lopencv_videostab -lopencv_videoio -lopencv_viz -lopencv_ximgproc -lopencv_video -lopencv_dnn -lopencv_xobjdetect -lopencv_objdetect -lopencv_calib3d -lopencv_imgcodecs -lopencv_features2d -lopencv_flann -lopencv_xphoto -lopencv_photo -lopencv_imgproc -lopencv_core


main: $(SRC_DIR)/cam.cpp $(SRC_DIR)/odcs.c utils.o
	gcc -o odcs.out $(SRC_DIR)/odcs.c utils.o -lrt -lpthread -ljson-c -lonion $(OPT)
	g++ -o cam.out $(SRC_DIR)/cam.cpp utils.o -I/usr/local/include/opencv4 -lopencv_videoio -lopencv_imgproc -lopencv_core -lopencv_imgcodecs -lrt -lpthread -ljson-c $(OPT)


utils.o: $(SRC_DIR)/utils.c $(SRC_DIR)/utils.h
	gcc $(SRC_DIR)/utils.c -c $(OPT)

clean:
	rm *.out *.o
