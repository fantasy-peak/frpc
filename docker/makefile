ROCKY_LINUX_VERSIONS := 8.9.20231119
REPOSITORY_NAME := rockylinux-frpc
CONTAINER := "rockylinux-frpc"

.PONY: $(ROCKY_LINUX_VERSIONS)
default: $(ROCKY_LINUX_VERSIONS)


$(ROCKY_LINUX_VERSIONS):
	@echo "----(${REPOSITORY_NAME}:${ROCKY_LINUX_VERSIONS})"
	# docker build --progress=plain --no-cache --build-arg ROCKY_LINUX_VERSIONS=$@ -t ${REPOSITORY_NAME}:$@ .
	docker build --progress=plain --build-arg ROCKY_LINUX_VERSIONS=$@ -t ${REPOSITORY_NAME}:$@ .

del:
	docker rm -f ${CONTAINER}
	docker rmi -f ${REPOSITORY_NAME}:${ROCKY_LINUX_VERSIONS}

run:
	docker run -it -v /var/run/docker.sock:/var/run/docker.sock --net=host --privileged --name ${CONTAINER} ${REPOSITORY_NAME}:${ROCKY_LINUX_VERSIONS}

login:
	docker start $(CONTAINER)
	docker exec -i -t $(CONTAINER) /bin/bash

