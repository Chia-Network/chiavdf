#include "vdf_base.hpp"

Prover::Prover(Segment segm, integer D) {
    this->segm = segm;
    this->D = D;
    this->num_iterations = segm.length;
    is_finished = false;
}

bool Prover::IsFinished() {
    return is_finished;
}

form Prover::GetProof() {
    return proof;
}

ParallelProver::ParallelProver(Segment segm, integer D) : Prover(segm, D) {}
