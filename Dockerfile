FROM ubuntu:22.04
RUN apt-get update && apt-get install -y gcc libsqlite3-dev
WORKDIR /app
COPY . .
# Compile main.c and mongoose.c together as a Pure C app
RUN gcc main.c mongoose.c -o exam_app -lsqlite3 -lpthread -lm
EXPOSE 18080
CMD ["./exam_app"]