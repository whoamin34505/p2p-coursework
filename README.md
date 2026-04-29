# P2P Coursework

Decentralized P2P file exchange system with encryption and logging.

## Build

```bash
make
````

## Run

On node1:

```bash
./p2p_node node1 5000
```

On node2:

```bash
./p2p_node node2 5000
```

On node3:

```bash
./p2p_node node3 5000
```

On node4:

```bash
./p2p_node node4 5000
```

## Directories

* `shared/` contains files available for upload.
* `downloads/` contains received files.
* `node.log` contains node events and errors.

## Commands inside the program

```text
help
peers
find <filename>
get <filename>
exit
```
## Before use:
```bash
sudo apt update
sudo apt install build-essential make gcc libssl-dev
```
