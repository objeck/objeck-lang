FROM debian
ENV http_proxy=deb.debian.org
RUN apt-get update && apt-get upgrade -y && apt-get -y install \
	vim libssl-dev unixodbc-dev libsdl2-dev \
	libsdl2-image-dev libsdl2-ttf-dev \
	libsdl2-mixer-dev libreadline-dev unzip libeigen3-dev
COPY deploy /opt/objeck/
ENV PATH="${PATH}:/opt/objeck/bin"
ENV OBJECK_LIB_PATH="/opt/objeck/lib"
RUN mkdir -p /home/objeck
WORKDIR /home/objeck

# CMD obc -in "\"Hello World\"->PrintLine()" && \
# 	obr program.obe
# ENTRYPOINT ["/bin/bash"]