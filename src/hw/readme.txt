Build instructions:

Start in the top dir of 'chiavdf' repo.

cd src/hw
# create symlink to libftd2xx package
ln -s ../../path/to/libftd2xx
cd ..
make -f Makefile.vdf-client emu_hw_main hw_main emu_hw_vdf_client hw_vdf_client

---

Running with emulated HW:
./emu_hw_main 3 100000

Running with real HW:
./hw_main 3 1000000

The arguments are the number of VDF engines (1, 2 or 3) and the number of iterations to run.

---

To run with an emulated timelord, first build 'chiavdf' Python module:
# start in the top chiavdf directory
python3 setup.py build

Start emulated timelord (replace 3.7 in directory name if your Python version is not 3.7):
PYTHONPATH=build/lib.linux-x86_64-3.7 python3 src/tl_emu.py

Start VDF client in another terminal (8000 is the TCP port number):
./hw_vdf_client 8000

Alternatively, run with the emulated HW:
./emu_hw_vdf_client 8000

---

To run the hardware VDF client with a Chia timelord in testnet:

Install Chia blockchain:
https://github.com/Chia-Network/chia-blockchain/wiki/INSTALL

Configure testnet10 (or a different testnet as needed):
https://github.com/Chia-Network/chia-blockchain/wiki/How-to-Connect-to-the-Testnet

Start a local full node (in chia-blockchain directory):
venv/bin/chia start node

Change log level in config file (it is likely located in ~/.chia/mainnet/config/config.yaml):
...
    log_level: INFO

Start timelord (in chia-blockchain directory):
venv/bin/chia start timelord-only

Start HW VDF client (in chiavdf/src directory):
./hw_vdf_client 8000 3

Watch timelord logs, the following lines would indicate the proofs are coming:
tail -f ~/.chia/mainnet/log/debug.log
...
2023-02-10T17:54:46.719 timelord chia.timelord.timelord   : INFO     Finished PoT chall:627fdf36c81a1399393f.. 1720038 iters, Estimated IPS: 53739.7, Chain: Chain.CHALLENGE_CHAIN
2023-02-10T17:54:48.581 timelord chia.timelord.timelord   : INFO     Finished PoT chall:7ea19c8a61ee0691c1d6.. 1720038 iters, Estimated IPS: 50785.0, Chain: Chain.REWARD_CHAIN

To stop running hardware VDF client and timelord, first stop the VDF client using Ctrl-C, then shut down the timelord:
venv/bin/chia stop timelord-only
