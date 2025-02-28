#!/usr/bin/python3

from daemons import Daemon, Wallet
import ethereum
from ethereum import ServiceNodeRewardContract, ContractSeedServiceNode, ContractServiceNodeContributor

import pathlib
import argparse
import time
import shutil
import os
import asyncio
from   datetime import datetime
import subprocess
import atexit
import concurrent.futures
from typing import List

datadirectory="testdata"

def coins(*args):
    if len(args) != 1:
        return tuple(coins(x) for x in args)
    x = args[0]
    if type(x) in (tuple, list):
        return type(x)(coins(i) for i in x)
    return round(x * 1000000000)


def wait_for(callback, timeout=10):
    expires = time.time() + timeout
    while True:
        try:
            if callback():
                return
        except:
            pass
        if time.time() >= expires:
            raise RuntimeError("task timeout expired")
        time.sleep(.25)


verbose = True
def vprint(*args, timestamp=True, **kwargs):
    global verbose
    if verbose:
        if timestamp:
            print(datetime.now(), end=" ")
        print(*args, **kwargs)

def all_service_nodes_proofed(sn):
    service_nodes = sn.json_rpc("get_n_service_nodes", {"fields": {"quorumnet_port": True, "pubkey_bls": True}}).json()['result']['service_node_states']
    result = True
    vprint("  {}".format(service_nodes), timestamp=False)
    for x in service_nodes:
        if x['quorumnet_port'] <= 0 or 'pubkey_bls' not in x or x['pubkey_bls'] is None:
            result = False
    return result

class SNNetwork:
    def __init__(self, datadir, *, oxen_bin_dir, anvil_path, eth_sn_contracts_dir, sns=12, nodes=3):
        begin_time = time.perf_counter()

        # Setup directories
        self.datadir      = datadir
        self.oxen_bin_dir = oxen_bin_dir
        if not os.path.exists(self.datadir):
            os.makedirs(self.datadir)

        # Setup Anvil, a private Ethereum blockchain (if specified)
        if anvil_path is not None:
            if os.path.exists(anvil_path):
                self.anvil = subprocess.Popen(anvil_path,
                                              stdin=subprocess.DEVNULL,
                                              stdout=subprocess.DEVNULL,
                                              stderr=subprocess.DEVNULL)
            else:
                raise RuntimeError('Anvil path \'{}\' specified but does not exist. Exiting'.format(anvil_path))

        # Verify private Ethereum blockchain is reachable
        verify_private_blockchain_attempts = 4
        while verify_private_blockchain_attempts > 0:
            try:
                eth_chain_id = ethereum.eth_chainId()
                assert eth_chain_id == 31337, 'Private Ethereum instance did not return correct chain ID {}'.format(eth_chain_id)
            except RuntimeError:
                verify_private_blockchain_attempts -= 1
                time.sleep(0.25)
                if verify_private_blockchain_attempts == 0:
                    raise
            else:
                break

        # Deploy smart contracts from eth-sn-contracts (if specified)
        if eth_sn_contracts_dir is not None:
            eth_sn_contracts_makefile_path = eth_sn_contracts_dir / 'Makefile'
            if os.path.exists(eth_sn_contracts_makefile_path):
                subprocess.run(['make', 'deploy-local'],
                               cwd=eth_sn_contracts_dir,
                               check=True)
            else:
                raise RuntimeError('eth-sn-contracts expected file to exist \'{}\' but does not. Exiting'.format(anvil_path))

        # Connect rewards contract proxy to blockchain instance
        self.sn_contract = ServiceNodeRewardContract()

        vprint("Using '{}' for data files and logs".format(datadir))
        nodeopts = dict(oxend=str(self.oxen_bin_dir / 'oxend'), datadir=datadir)

        self.ethsns = [Daemon(service_node=True, **nodeopts) for _ in range(1)]
        self.sns    = [Daemon(service_node=True, **nodeopts) for _ in range(sns)]
        self.nodes  = [Daemon(**nodeopts) for _ in range(nodes)]

        self.all_nodes = self.sns + self.nodes + self.ethsns

        self.wallets = []
        for name in ('Alice', 'Bob', 'Mike'):
            self.wallets.append(Wallet(
                node=self.nodes[len(self.wallets) % len(self.nodes)],
                name=name,
                rpc_wallet=str(self.oxen_bin_dir/'oxen-wallet-rpc'),
                datadir=datadir))

        self.alice, self.bob, self.mike = self.wallets

        self.extrawallets = []
        for name in range(9):
            self.extrawallets.append(Wallet(
                node=self.nodes[len(self.extrawallets) % len(self.nodes)],
                name="extrawallet-"+str(name),
                rpc_wallet=str(self.oxen_bin_dir/'oxen-wallet-rpc'),
                datadir=datadir))

        # Interconnections
        for i in range(len(self.all_nodes)):
            for j in (2, 3, 5, 7, 11):
                k = (i + j) % len(self.all_nodes)
                if i != k:
                    self.all_nodes[i].add_peer(self.all_nodes[k])

        # Thread Pool ##############################################################################
        thread_pool                              = concurrent.futures.ThreadPoolExecutor()
        futures: List[concurrent.futures.Future] = []

        # Start Oxen SNs ###########################################################################
        vprint("Starting new oxend service nodes with RPC".format(self.sns[0].listen_ip), end="")
        for sn in self.sns:
            futures.append(thread_pool.submit(sn.start))
        for sn in self.ethsns:
            futures.append(thread_pool.submit(sn.start))

        concurrent.futures.wait(futures)
        futures.clear()

        for sn in self.sns:
            vprint(" {}".format(sn.rpc_port), end="", flush=True, timestamp=False)

        for sn in self.ethsns:
            vprint(" {}".format(sn.rpc_port), end="", flush=True, timestamp=False)

        # Start Oxen Nodes #########################################################################
        vprint(timestamp=False)
        vprint("Starting new regular oxend nodes with RPC on {} ports".format(self.nodes[0].listen_ip), end="")
        for d in self.nodes:
            vprint(" {}".format(d.rpc_port), end="", flush=True, timestamp=False)
            d.start()
        vprint(timestamp=False)

        vprint("Waiting for all oxend's to get ready")
        for d in self.all_nodes:
            d.wait_for_json_rpc("get_info")

        vprint("Oxends are ready. Starting wallets in parallel")
        # Start wallet executables #################################################################
        for w in self.wallets:
            vprint("Starting new RPC wallet {w.name} at {w.listen_ip}:{w.rpc_port}".format(w=w))
            thread_pool.submit(w.start)

        for w in self.extrawallets:
            vprint("Starting new RPC wallet {w.name} at {w.listen_ip}:{w.rpc_port}".format(w=w))
            thread_pool.submit(w.start)

        concurrent.futures.wait(futures)
        futures.clear()

        # Create wallets ###########################################################################
        for w in self.wallets:
            futures.append(thread_pool.submit(w.ready))

        for w in self.extrawallets:
            futures.append(thread_pool.submit(w.ready))

        concurrent.futures.wait(futures)
        futures.clear()

        # Refresh wallets ##########################################################################
        for w in self.wallets:
            w.refresh()
            vprint("Wallet {w.name} is ready: {a}".format(w=w, a=w.address()))

        for w in self.extrawallets:
            w.refresh()
            vprint("Wallet {w.name} is ready: {a}".format(w=w, a=w.address()))

        for w in self.wallets:
            w.wait_for_json_rpc("refresh")

        for w in self.extrawallets:
            w.wait_for_json_rpc("refresh")

        configfile=self.datadir+'config.py'
        with open(configfile, 'w') as filetowrite:
            filetowrite.write('#!/usr/bin/python3\n# -*- coding: utf-8 -*-\nlisten_ip=\"{}\"\nlisten_port=\"{}\"\nwallet_listen_ip=\"{}\"\nwallet_listen_port=\"{}\"\nwallet_address=\"{}\"\nexternal_address=\"{}\"'.format(self.sns[0].listen_ip,self.sns[0].rpc_port,self.mike.listen_ip,self.mike.rpc_port,self.mike.address(),self.bob.address()))

        # Start blockchain setup ###################################################################
        # Mine some blocks; we need 100 per SN registration, and we can nearly 600 on fakenet before
        # it hits HF16 and kills mining rewards.  This lets us submit the first 5 SN registrations a
        # SN (at height 40, which is the earliest we can submit them without getting an occasional
        # spurious "Not enough outputs to use" error).
        # to unlock and the rest to have enough unlocked outputs for mixins), then more some more to
        # earn SN rewards.  We need 100 per SN registration, and each mined block gives us an input
        # of 18.9, which means each registration requires 6 inputs.  Thus we need a bare minimum of
        # 6(N-5) blocks, plus the 30 lock time on coinbase TXes = 6N more blocks (after the initial
        # 5 registrations).
        self.sync_nodes(self.mine(46), timeout=120)
        vprint("Submitting first round of service node registrations:", flush=True)
        self.mike.refresh()
        for sn in self.sns[0:5]:
            self.mike.register_sn(sn, self.sns[0].get_staking_requirement())
            vprint(".", end="", flush=True, timestamp=False)
        vprint(timestamp=False)
        if len(self.sns) > 5:
            vprint("Going back to mining", flush=True)

            self.mine(6*len(self.sns))

            self.print_wallet_balances()
            self.mike.transfer(self.alice, coins(150))
            self.mike.transfer(self.bob, coins(150))
            vprint("Submitting more service node registrations: ", end="", flush=True)
            for sn in self.sns[5:-1]:
                self.mike.register_sn(sn, self.sns[0].get_staking_requirement())
                vprint(".", end="", flush=True, timestamp=False)
            vprint(timestamp=False)
            vprint("Done.")

        self.print_wallet_balances()

        vprint("Mining 30 blocks to height 149 (registrations + blink quorum lag) and waiting for nodes to sync")
        self.sync_nodes(self.mine(29), timeout=120)
        for wallet in self.extrawallets:
            self.mike.transfer(wallet, coins(11))
        self.sync_nodes(self.mine(1), timeout=120) # Height 149

        self.print_wallet_balances()

        # Register the last SN through Bobs wallet (Has not done any others)
        # and also get 9 other wallets to contribute the rest of the node with a 10% operator fee
        self.bob.register_sn_for_contributions(sn=self.sns[-1], cut=10, amount=coins(28), staking_requirement=self.sns[0].get_staking_requirement())
        self.sync_nodes(self.mine(21), timeout=120) # Height 170
        self.print_wallet_balances()
        for wallet in self.extrawallets:
            wallet.contribute_to_sn(self.sns[-1], coins(8))

        vprint("Sending fake lokinet/ss pings")
        for sn in self.sns:
            sn.ping()

        vprint("Send uptime proofs at height HF20 to propagate BLS pubkeys")
        for sn in self.sns:
            sn.send_uptime_proof()

        vprint("Waiting for proofs to propagate:", flush=True)
        for sn in self.sns:
            wait_for(lambda: all_service_nodes_proofed(sn), timeout=120)
        vprint(timestamp=False)

        self.sync_nodes(self.mine(1), timeout=120) # Height 171

        # Pull out some useful keys to local variables
        sn0_pubkey            = self.ethsns[0].get_service_keys().pubkey
        hardhat_account       = self.sn_contract.hardhatAccountAddress()
        hardhat_account_no_0x = hardhat_account[2:42]
        assert len(hardhat_account) == 42, "Expected Eth address w/ 0x prefix + 40 hex characters. Account was {} ({} chars)".format(hardhat_account, len(hardhat_account))

        # Construct the seed list for initiating the smart contract.
        # Note all SNs up to this point (HF < feature::ETH_BLS) had a 100 OXEN staking requirement
        oxen_staking_requirement     = self.sns[0].get_staking_requirement()
        contract_staking_requirement = self.sn_contract.stakingRequirement()
        seed_node_list      = []
        for sn in self.sns:
            node      = ContractSeedServiceNode(sn.get_service_keys().bls_pubkey)
            assert node.pubkey is not None
            contributors = sn.sn_status()["service_node_state"]["contributors"]
            total_staked = 0

            for entry in contributors:
                contributor              = ContractServiceNodeContributor()
                contributor.addr         = hardhat_account # Default to the hardhat account

                # Use the oxen amount proportionally as the SENT amount
                contributor.stakedAmount = int((entry["amount"] / oxen_staking_requirement * contract_staking_requirement))
                total_staked += contributor.stakedAmount

                node.contributors.append(contributor)

            # Assign any left over SENt to be staked to the operator
            left_over_to_be_staked = contract_staking_requirement - total_staked
            if left_over_to_be_staked > 0:
                node.contributors[0].stakedAmount += left_over_to_be_staked

            seed_node_list.append(node)

        self.sn_contract.seedPublicKeyList(seed_node_list)
        vprint("Seeded BLS public keys into contract. Contract has {} SNs".format(self.sn_contract.numberServiceNodes()))

        # Start the rewards contract after seeding the BLS public keys
        self.sn_contract.start()

        # Register a SN via the Ethereum smart contract
        ethereum_add_bls_args = self.ethsns[0].get_ethereum_registration_args(hardhat_account_no_0x)
        vprint("Preparing to submit registration to Eth w/ address {} for SN {} ({})".format(hardhat_account,
                                                                                             sn0_pubkey,
                                                                                             ethereum_add_bls_args))
        self.sn_contract.addBLSPublicKey(ethereum_add_bls_args)

        # Advance the Arbitrum blockchain so that the SN registration is observed in oxen
        ethereum.evm_mine(self.sn_contract.web3)
        ethereum.evm_mine(self.sn_contract.web3)
        ethereum.evm_mine(self.sn_contract.web3)

        # NOTE: Log all the SNs in the contract ####################################################
        contract_sn_id_it = 0
        contract_sn_dump  = ""
        while True:
            contract_sn           = self.sn_contract.serviceNodes(contract_sn_id_it)
            contract_sn_dump += "  SN ID {} {}\n".format(contract_sn_id_it, vars(contract_sn))
            contract_sn_id_it     = contract_sn.next
            if contract_sn_id_it == 0:
                break

        # Verify registration was successful
        contract_sn_count = self.sn_contract.numberServiceNodes()
        vprint("Added node via Eth. Contract has {} SNs\n{}".format(contract_sn_count, contract_sn_dump))
        assert self.sn_contract.numberServiceNodes() == 13, f"Expected 13 service nodes, received {contract_sn_count}"

        # Sleep and let pulse quorum do work
        vprint(f"Sleeping now, awaiting pulse quorum to generate blocks, blockchain height is {self.ethsns[0].height()}");

        total_sleep_time = 0
        sleep_time       = 8
        target_height = self.ethsns[0].height() + 2;

        while self.ethsns[0].height() < target_height:
            total_sleep_time += sleep_time
            time.sleep(sleep_time)

        vprint(f"Waking up after sleeping for {total_sleep_time}s, blockchain height is {self.ethsns[0].height()}");

        # NOTE: BLS rewards claim ##################################################################
        # Claim rewards for Address
        rewards = self.ethsns[0].get_bls_rewards(hardhat_account_no_0x)
        vprint(rewards)
        rewardsAccount = rewards["result"]["address"]
        assert rewardsAccount.lower() == hardhat_account_no_0x.lower(), f"Rewards account '{rewardsAccount.lower()}' does not match hardhat account '{hardhat_account_no_0x.lower()}'. We have the private key for the hardhat account and use it to claim rewards from the contract"

        vprint("Contract rewards before updating has ['available', 'claimed'] respectively: ",
               self.sn_contract.recipients(hardhat_account),
               " for ",
               hardhat_account_no_0x)

        # TODO: We send the required balance from the hardhat account to the
        # contract to guarantee that claiming will succeed. We should hook up
        # the pool to the rewards contract and fund the contract from there.
        unsent_tx = self.sn_contract.erc20_contract.functions.transfer(self.sn_contract.contract_address, rewards["result"]["amount"] + 100).build_transaction({
            "from": self.sn_contract.acc.address,
            'nonce': self.sn_contract.web3.eth.get_transaction_count(self.sn_contract.acc.address)})
        signed_tx = self.sn_contract.web3.eth.account.sign_transaction(unsent_tx, private_key=self.sn_contract.acc.key)
        self.sn_contract.web3.eth.send_raw_transaction(signed_tx.raw_transaction)
        self.sn_contract.foundation_pool_contract.functions.payoutReleased().call()

        vprint("Foundation pool balance: {}".format(self.sn_contract.erc20balance(self.sn_contract.foundation_pool_address)))
        vprint("Rewards contract balance: {}".format(self.sn_contract.erc20balance(self.sn_contract.contract_address)))

        # NOTE: Then update the rewards blaance
        self.sn_contract.updateRewardsBalance(
                hardhat_account,
                rewards["result"]["amount"],
                rewards["result"]["signature"],
                rewards["result"]["non_signer_indices"])

        vprint("Contract rewards update executed, has ['available', 'claimed'] now respectively: ",
               self.sn_contract.recipients(hardhat_account),
               " for ",
               hardhat_account_no_0x)

        vprint("Balance for '{}' before claim {}".format(hardhat_account, self.sn_contract.erc20balance(hardhat_account)))

        # NOTE: Now claim the rewards
        self.sn_contract.claimRewards()
        vprint("Contract rewards after claim is now ['available', 'claimed'] respectively: ",
               self.sn_contract.recipients(hardhat_account),
               " for ",
               hardhat_account)
        vprint("Balance for '{}' after claim {}".format(hardhat_account, self.sn_contract.erc20balance(hardhat_account)))

        # Initiate Removal of BLS Key ##############################################################
        # sn_to_remove_index       = random.randint(0, len(self.sns) - 1)
        # sn_to_remove_bls_pubkey  = self.sns[sn_to_remove_index].get_service_keys().bls_pubkey
        # sn_to_remove_contract_id = self.sn_contract.getServiceNodeID(sn_to_remove_bls_pubkey)

        # vprint("Randomly chose to remove SN {} with BLS key {}, submitting request".format(sn_to_remove_contract_id, sn_to_remove_bls_pubkey))
        # self.sn_contract.initiateRemoveBLSPublicKey(sn_to_remove_contract_id)

        # days_30_in_seconds = 60 * 60 * 24 * 30
        # ethereum.evm_increaseTime(self.sn_contract.web3, days_30_in_seconds)
        # ethereum.evm_mine(self.sn_contract.web3)

        # Exit Node ################################################################################
        # exit_request = self.ethsns[0].get_exit_request(sn_to_remove_bls_pubkey)
        # vprint("Exit request aggregated: {}".format(exit_request))
        # self.sn_contract.removeBLSPublicKeyWithSignature(exit_request["result"]["bls_pubkey"],
        #                                                  exit_request["result"]["timestamp"],
        #                                                  exit_request["result"]["signature"],
        #                                                  exit_request["result"]["non_signer_indices"])

        # Liquidate Node ###########################################################################
        # exit = self.ethsns[0].get_liquidation_request(ethereum_add_bls_args["bls_pubkey"])
        # result = self.sn_contract.liquidateBLSPublicKeyWithSignature(
                # exit["result"]["bls_key"],
                # exit["result"]["signature"],
                # exit["result"]["non_signers_bls_pubkeys"])
        # vprint(result)
        # vprint("Submitted transaction to liquidate service node : {}".format(ethereum_add_bls_args["bls_pubkey"]))
        # vprint("liquidated node: number of service nodes in contract {}".format(self.sn_contract.numberServiceNodes()))

        elapsed_time = time.perf_counter() - begin_time
        vprint("Local Devnet SN network setup complete in {}s!".format(elapsed_time))
        vprint("Communicate with daemon on ip: {} port: {}".format(self.sns[0].listen_ip,self.sns[0].rpc_port))

    def refresh_wallets(self, *, extra=[]):
        vprint("Refreshing wallets")
        for w in self.wallets + extra:
            w.refresh()
        vprint("All wallets refreshed")


    def mine(self, blocks=None, wallet=None, *, sync=False):
        """Mine some blocks to the given wallet (or self.mike if None) on the wallet's daemon.
        Returns the daemon's height after mining the blocks.  If blocks is omitted, mines enough to
        confirm regular transfers (i.e. 10 blocks).  If sync is specified, sync all nodes and then
        refresh all wallets after mining."""
        if wallet is None:
            wallet = self.mike
        if blocks is None:
            blocks = 10
        node = wallet.node
        vprint("Mining {} blocks to wallet {.name}".format(blocks, wallet))
        start_height = node.height()
        end_height = start_height + blocks
        node.mine_blocks(blocks, wallet)
        while node.rpc("/mining_status").json()["active"]:
            height = node.height()
            vprint("Mined {}/{}".format(height, end_height))
            time.sleep(0.05 if height >= end_height else 0.25)
        height = node.height()
        vprint("Mined {}/{}".format(height, end_height))

        if sync:
            self.sync_nodes(height)
            self.refresh_wallets()

        return height


    def sync_nodes(self, height=None, *, extra=[], timeout=10):
        """Waits for all nodes to reach the given height, typically invoked after mine()"""
        nodes = self.all_nodes + extra
        heights = [x.height() for x in nodes]
        if height is None:
            height = max(heights)
        if min(heights) >= height:
            vprint("All nodes already synced to height >= {}".format(height))
            return
        vprint("Waiting for all nodes to sync to height {}".format(height))
        last = None
        expiry = time.time() + timeout
        while nodes and time.time() < expiry:
            if heights[-1] < height:
                heights[-1] = nodes[-1].height()
            if heights[-1] >= height:
                heights.pop()
                nodes.pop()
                last = None
                continue
            if heights[-1] != last:
                vprint("waiting for {} [{} -> {}]".format(nodes[-1].name, heights[-1], height))
                last = heights[-1]
            time.sleep(0.1)
        if nodes:
            raise RuntimeError("Timed out waiting for node syncing")
        vprint("All nodes synced to height {}".format(height))


    def sync(self, extra_nodes=[], extra_wallets=[]):
        """Synchronizes everything: waits for all nodes to sync, then refreshes all wallets.  Can be
        given external wallets/nodes to sync."""
        self.sync_nodes(extra=extra_nodes)
        self.refresh_wallets(extra=extra_wallets)


    def print_wallet_balances(self):
        """Instructs the wallets to refresh and prints their balances (does nothing in non-verbose mode)"""
        global verbose
        if not verbose:
            return
        vprint("Balances:")
        for w in self.wallets:
            b = w.balances(refresh=True)
            vprint("    {:5s}: {:.9f} (total) with {:.9f} (unlocked)".format(
                w.name, b[0] * 1e-9, b[1] * 1e-9))
        for w in self.extrawallets:
            b = w.balances(refresh=True)
            vprint("    {:5s}: {:.9f} (total) with {:.9f} (unlocked)".format(
                w.name, b[0] * 1e-9, b[1] * 1e-9))


    def __del__(self):
        for n in self.all_nodes:
            n.terminate()
        for w in self.wallets:
            w.terminate()
        if self.anvil is not None:
            self.anvil.terminate()

snn = None

def run():
    arg_parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    arg_parser.add_argument('--oxen-bin-dir',
                            help=('Set the directory where Oxen binaries (oxend, wallet rpc, ...) '
                                  'are located.'),
                            default="../../build/bin",
                            type=pathlib.Path)
    arg_parser.add_argument('--anvil-path',
                            help=('Set the path to Foundry\'s `anvil` for launching a private '
                                  'Ethereum blockchain. If omitted a private Ethereum node must be '
                                  'running at localhost:8545.'),
                            type=pathlib.Path)
    arg_parser.add_argument('--eth-sn-contracts-dir',
                            help=('Set the path to Oxen\'s `eth-sn-contracts` repository is '
                                  'located. The script will programmatically launch and deploy the '
                                  'contracts specified via `make deploy-local`. If omitted, the '
                                  'private Ethereum blockchain must already be deployed with the '
                                  'smart contracts prior to invoking this script.'),
                            type=pathlib.Path)
    args = arg_parser.parse_args()

    if args.anvil_path is not None:
        if args.eth_sn_contracts_dir is None:
            raise RuntimeError('--eth-sn-contracts-dir must be specified when --anvil-path is set')

    atexit.register(cleanup)
    global snn, verbose
    if not snn:
        if os.path.isdir(datadirectory+'/'):
            shutil.rmtree(datadirectory+'/')
        vprint("new SNN")
        snn = SNNetwork(oxen_bin_dir=args.oxen_bin_dir,
                        anvil_path=args.anvil_path,
                        eth_sn_contracts_dir=args.eth_sn_contracts_dir,
                        datadir=datadirectory+'/')
    else:
        vprint("reusing SNN")
        snn.alice.new_wallet()
        snn.bob.new_wallet()

        # Flush pools because some tests leave behind impossible txes
        for n in snn.all_nodes:
            assert n.json_rpc("flush_txpool").json()['result']['status'] == 'OK'

        # Mine a few to clear out anything in the mempool that can be cleared
        snn.mine(5, sync=True)

        vprint("Alice has new wallet: {}".format(snn.alice.address()))
        vprint("Bob   has new wallet: {}".format(snn.bob.address()))

    input("Use Ctrl-C to exit...")
    loop = asyncio.get_event_loop()
    try:
        loop.run_forever()
    except KeyboardInterrupt:
        print(f'!!! AsyncApplication.run: got KeyboardInterrupt during start')
    finally:
        loop.close()

def cleanup():
    if snn is not None and snn.anvil is not None:
        snn.anvil.terminate()

# Shortcuts for accessing the named wallets
def alice(net):
    return net.alice

def bob(net):
    return net.bob

def mike(net):
    return net.mike

if __name__ == '__main__':
    run()
