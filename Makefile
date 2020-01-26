


INC = -I../../Include -I/usr/include/libdrm/
CDIR = /home/pi/RPiBook/opengles3-book-master/build/Common/CMakeFiles/Common.dir/Source/

esUtil_DRM.c.o: esUtil_DRM.c
	cc -c -o esUtil_DRM.c.o esUtil_DRM.c $(INC)
	/usr/bin/ar qc libCommon.a  $(CDIR)esShader.c.o $(CDIR)esShapes.c.o $(CDIR)esTransform.c.o $(CDIR)esUtil.c.o esUtil_DRM.c.o
	/usr/bin/ranlib libCommon.a
	cp libCommon.a /home/pi/RPiBook/opengles3-book-master/build/Common/libCommon.a
