#pragma once

#include <vector>

#include "SymbolicSNode.hxx"
#include "kernels/factor.hxx"
#include "kernels/assemble.hxx"

#include <starpu.h>

namespace spldlt { namespace starpu {

      /* Register handles in StarPU*/

      template <typename T, typename PoolAlloc>
      void register_node(
            SymbolicSNode &snode,
            NumericNode<T, PoolAlloc> &node,
            int nb
            ) {

         int m = snode.nrow;
         int n = snode.ncol;
         T *a = node.lcol;
         int lda = align_lda<T>(m);
         int nr = (m-1) / nb + 1; // number of block rows
         int nc = (n-1) / nb + 1; // number of block columns
         // snode.handles.reserve(nr*nc);
         snode.handles.resize(nr*nc); // allocate handles

         for(int j = 0; j < nc; ++j) {
               
            int blkn = std::min(nb, n - j*nb);
               
            for(int i = j; i < nr; ++i) {
               
               int blkm = std::min(nb, m - i*nb);
                                 
               starpu_matrix_data_register(
                     &(snode.handles[i + j*nr]), // StarPU handle ptr 
                     STARPU_MAIN_RAM, // memory 
                     reinterpret_cast<uintptr_t>(&a[(j*nb)*lda+(i*nb)]),
                     lda, blkm, blkn,
                     sizeof(T));
            }
         }         
      }

      /* Unregister handles in StarPU*/
      // void unregister
      
      /* init_node StarPU task*/
      /* CPU task */
      template <typename T, typename PoolAlloc>
      void init_node_cpu_func(void *buffers[], void *cl_arg) {

         SymbolicSNode *snode = nullptr;
         NumericNode<T, PoolAlloc> *node = nullptr;
         T *aval;

         starpu_codelet_unpack_args(
               cl_arg,
               &snode,
               &node,
               &aval);

         init_node(*snode, *node, aval);
      }

      /* init_node codelet */
      struct starpu_codelet cl_init_node;      

      template <typename T, typename PoolAlloc>
      void insert_init_node(
            SymbolicSNode *snode,
            NumericNode<T, PoolAlloc> *node,
            starpu_data_handle_t node_hdl,
            T *aval) {
                  
         int ret;

         ret = starpu_insert_task(
               &cl_init_node,
               STARPU_RW, node_hdl,
               STARPU_VALUE, &snode, sizeof(SymbolicSNode*),
               STARPU_VALUE, &node, sizeof(NumericNode<T, PoolAlloc>*),
               STARPU_VALUE, &aval, sizeof(T*),
               0);

         STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_insert");

      }

      /* factorize_block StarPU task */

      /* CPU task */
      /* TODO generic prec */
      void factorize_block_cpu_func(void *buffers[], void *cl_arg) {
         
         double *blk = (double *)STARPU_MATRIX_GET_PTR(buffers[0]);
         unsigned m = STARPU_MATRIX_GET_NX(buffers[0]);
         unsigned n = STARPU_MATRIX_GET_NY(buffers[0]);
         unsigned ld = STARPU_MATRIX_GET_LD(buffers[0]);


         factorize_diag_block(m, n, blk, ld);
      }
      
      /* FIXME: although it would be better to statically initialize
         the codelet, it is not well suported by g++ */

      // struct starpu_codelet cl_factorize_block = {
      //    // .where = w,
      //    // .cpu_funcs = {factorize_block_cpu_func, NULL},
      //    .nbuffers = STARPU_VARIABLE_NBUFFERS,
      //    // .name = "FACTO_BLK"
      // };

      /* factorize block codelet */
      struct starpu_codelet cl_factorize_block;      

      /* Insert factorization of diag block into StarPU*/
      void insert_factorize_block(
            starpu_data_handle_t bc_hdl,
            starpu_data_handle_t node_hdl) {
                  
         int ret;

         if (node_hdl) {
            ret = starpu_insert_task(
                  &cl_factorize_block,
                  STARPU_RW, bc_hdl,
                  STARPU_R, node_hdl,
                  0);
         }
         else {
            ret = starpu_insert_task(
                  &cl_factorize_block,
                  STARPU_RW, bc_hdl,
                  0);
         }

         STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_insert");
      }

      /* solve_block StarPU task */

      /* CPU task */
      void solve_block_cpu_func(void *buffers[], void *cl_arg) {

         /* Get diag block pointer and info */
         double *blk = (double *)STARPU_MATRIX_GET_PTR(buffers[0]);
         unsigned ld = STARPU_MATRIX_GET_LD(buffers[0]);

         /* Get sub diag block pointer and info */
         double *blk_ik = (double *)STARPU_MATRIX_GET_PTR(buffers[1]);
         unsigned m = STARPU_MATRIX_GET_NX(buffers[1]);
         unsigned n = STARPU_MATRIX_GET_NY(buffers[1]);
         unsigned ld_ik = STARPU_MATRIX_GET_LD(buffers[1]);

         /* Call kernel function */
         solve_block(m, n, blk, ld, blk_ik, ld_ik);
      }

      /* solve_block codelet */
      struct starpu_codelet cl_solve_block;

      /* Insert solve of sub diag block into StarPU */
      void insert_solve_block(
            starpu_data_handle_t bc_kk_hdl, /* diag block handle */
            starpu_data_handle_t bc_ik_hdl /* sub diag block handle */
            ) {

         int ret;

         ret = starpu_insert_task(
               &cl_solve_block,
               STARPU_R, bc_kk_hdl,
               STARPU_RW, bc_ik_hdl,
               0);

         STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_insert");
      }

      /* update_block StarPU task */

      /* CPU task */
      void update_block_cpu_func(void *buffers[], void *cl_arg) {

         /* Get A_ij pointer */
         double *blk_ij = (double *)STARPU_MATRIX_GET_PTR(buffers[0]);
         unsigned m = STARPU_MATRIX_GET_NX(buffers[0]);
         unsigned n = STARPU_MATRIX_GET_NY(buffers[0]);
         unsigned ld_ij = STARPU_MATRIX_GET_LD(buffers[0]);
                  
         /* Get A_ik pointer */
         double *blk_ik = (double *)STARPU_MATRIX_GET_PTR(buffers[1]);
         unsigned k = STARPU_MATRIX_GET_NY(buffers[1]);
         unsigned ld_ik = STARPU_MATRIX_GET_LD(buffers[1]); 

         double *blk_jk = (double *)STARPU_MATRIX_GET_PTR(buffers[2]);
         unsigned ld_jk = STARPU_MATRIX_GET_LD(buffers[2]); 
        
         update_block(m, n, blk_ij, ld_ij,
                      k,
                      blk_ik, ld_ik, 
                      blk_jk, ld_jk);

      }

      /* update block codelet */
      struct starpu_codelet cl_update_block;      

      void insert_update_block(
            starpu_data_handle_t bc_ij_hdl, /* A_ij block handle */
            starpu_data_handle_t bc_ik_hdl, /* A_ik block handle */
            starpu_data_handle_t bc_jk_hdl /* A_jk block handle */
            ) {

         int ret;

         ret = starpu_insert_task(
               &cl_update_block,
               STARPU_RW, bc_ij_hdl,
               STARPU_R, bc_ik_hdl,
               STARPU_R, bc_jk_hdl,
               0);

         STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_insert");         
      }

      /* update_between StarPU task */

      /* CPU task */

      template <typename T, typename PoolAlloc>
      void update_between_cpu_func(void *buffers[], void *cl_arg) {

         /* Get workspace */
         T *buffer = (T *)STARPU_MATRIX_GET_PTR(buffers[0]);
         // unsigned buf_m = STARPU_MATRIX_GET_NX(buffers[0]);
         // unsigned buf_n = STARPU_MATRIX_GET_NY(buffers[0]);
         // unsigned buf_ld = STARPU_MATRIX_GET_LD(buffers[0]);
         // printf("[update_between_cpu_func] buf_m: %d, buf_n: %d, buf_ld: %d\n", buf_m, buf_n, buf_ld);
         /* Get rowmap workspace */
         int *rlst = (int *)STARPU_VECTOR_GET_PTR(buffers[1]);
         /* Get colmap workspace */
         int *clst = (int *)STARPU_VECTOR_GET_PTR(buffers[2]);
         /* Get A_ij block */
         T *a_ij = (T *)STARPU_MATRIX_GET_PTR(buffers[3]);
         unsigned lda = STARPU_MATRIX_GET_LD(buffers[3]);
         /* Get any A_ik block width */ 
         // unsigned n = STARPU_MATRIX_GET_NY(buffers[4]);
        
         int kk, ii, jj;
         int blksz;
         int cptr, cptr2;
         int rptr, rptr2;

         SymbolicSNode *snode = nullptr, *asnode = nullptr;
         NumericNode<T, PoolAlloc> *node = nullptr;

         starpu_codelet_unpack_args(
               cl_arg,
               &snode,
               &node,
               &kk,
               &asnode,
               &ii, &jj,
               &blksz,
               &cptr, &cptr2,
               &rptr, &rptr2
               );

         // printf("[update_between_cpu_func] n: %d\n", n);
         // printf("[update_between_cpu_func] kk: %d, ii: %d, jj: %d\n", kk, ii, jj);
         // printf("[update_between_cpu_func] snode: %p\n", snode);
         // printf("[update_between_cpu_func]  snode nrow: %d\n", snode->nrow);
         // printf("[update_between_cpu_func] asnode nrow: %d\n", asnode->nrow);

         // int mc = cptr2-cptr+1; // number of rows in Ajk
         // int mr = rptr2-rptr+1; // number of rows in Aik
         // printf("[update_between_cpu_func] mr: %d, mc: %d, n: %d, blksz: %d\n", mr, mc, n, blksz);
         // rlst = new int[mr]; // Get ptr on rowmap array
         // clst = new int[mc];
         // buffer = new T[mr*mc];
         // printf("[update_between_cpu_func] buffer: %p\n", buffer);
         // for (int j = 0; j < blksz; j++)
         //    for (int i = 0; i < blksz; i++)
         //       buffer[j*blksz+i] = 0.0;

         // Determine A_ik and A_kj column width 
         int blkn = std::min(snode->ncol - kk*blksz, blksz);
         // printf("[update_between_cpu_func] blkn: %d\n", blkn);

         update_between_block(
               blkn, // blcok column width
               kk, ii, jj, // block row and block column index of A_ij block in destination node
               blksz, // block size
               cptr, cptr2, // local row indexes of a_kj elements 
               // in source node
               rptr, rptr2, // local row indexes of a_ik elements 
               // in source node
               *snode, // symbolic source node  
               *node, // numeric source node
               *asnode,  // symbolic destination node
               a_ij, lda, // block to be updated in destination node  
               buffer, // workspace
               rlst, clst // workpaces for col and row mapping
               );
      }

      /* update beween codelet */
      struct starpu_codelet cl_update_between;      
      
      /* Note that bc_ik_hdls and bc_jk_hdls hadles are used for coherency */
      template <typename T, typename PoolAlloc>
      void insert_update_between(
            SymbolicSNode *snode,
            NumericNode<T, PoolAlloc> *node,
            starpu_data_handle_t *bc_ik_hdls, int nblk_ik, /* A_ij block handle */
            starpu_data_handle_t *bc_jk_hdls, int nblk_jk, /* A_ik block handle */
            int kk,
            SymbolicSNode *asnode,
            starpu_data_handle_t anode_hdl,
            starpu_data_handle_t bc_ij_hdl,
            int ii, int jj,
            starpu_data_handle_t work_hdl,
            starpu_data_handle_t row_list_hdl,
            starpu_data_handle_t col_list_hdl,
            int blksz,
            int cptr, int cptr2,
            int rptr, int rptr2) {
         
         struct starpu_data_descr *descrs = 
            new starpu_data_descr[nblk_ik+nblk_jk+5];

         int ret;
         int nh = 0;
         
         // worksapce 
         descrs[nh].handle =  work_hdl; descrs[nh].mode = STARPU_SCRATCH;
         nh = nh + 1;
         // row_list
         descrs[nh].handle =  row_list_hdl; descrs[nh].mode = STARPU_SCRATCH;
         nh = nh + 1;
         // col_list
         descrs[nh].handle =  col_list_hdl; descrs[nh].mode = STARPU_SCRATCH;
         nh = nh + 1;
         // A_ij
         // printf("[insert_update_between] a_ij handle ptr: %p\n", bc_ij_hdl);
         descrs[nh].handle =  bc_ij_hdl; descrs[nh].mode = /* STARPU_RW;*/  (starpu_data_access_mode) (STARPU_RW | STARPU_COMMUTE);
         nh = nh + 1;
         // printf("[insert_update_between] nblk_ik: %d, nblk_jk: %d\n", nblk_ik, nblk_jk);
         // A_ik handles
         // DEBUG
         // printf("[insert_update_between] A_ik handle %d ptr: %p\n", 0, bc_ik_hdls[0]);
         // descrs[nh].handle = bc_ik_hdls[0];  descrs[nh].mode = STARPU_R;
         // nh = nh + 1;
         for(int i = 0; i < nblk_ik; i++) {
            // printf("[insert_update_between] a_ik handle %d, ptr: %p\n", i, bc_ik_hdls[i]);
            descrs[nh].handle = bc_ik_hdls[i];  descrs[nh].mode = STARPU_R;
            nh = nh + 1;
         }
         // A_jk handles
         for(int i = 0; i < nblk_jk; i++){
            // printf("[insert_update_between] a_jk handle %d, ptr: %p\n", i, bc_jk_hdls[i]);
            descrs[nh].handle = bc_jk_hdls[i];  descrs[nh].mode = STARPU_R;
            nh = nh + 1;
         }
         
         // Make sure that destination node as been initialized
         descrs[nh].handle =  anode_hdl; descrs[nh].mode = STARPU_R;
         nh = nh + 1;         

         ret = starpu_task_insert(&cl_update_between,
                                  STARPU_DATA_MODE_ARRAY, descrs,   nh,
                                  STARPU_VALUE, &snode, sizeof(SymbolicSNode*),
                                  STARPU_VALUE, &node, sizeof(NumericNode<T, PoolAlloc>*),
                                  STARPU_VALUE, &kk, sizeof(int),
                                  STARPU_VALUE, &asnode, sizeof(SymbolicSNode*),
                                  STARPU_VALUE, &ii, sizeof(int),
                                  STARPU_VALUE, &jj, sizeof(int),
                                  STARPU_VALUE, &blksz, sizeof(int),
                                  STARPU_VALUE, &cptr, sizeof(int),
                                  STARPU_VALUE, &cptr2, sizeof(int),
                                  STARPU_VALUE, &rptr, sizeof(int),
                                  STARPU_VALUE, &rptr2, sizeof(int),
                                  0);

         STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_insert");

      }

      /* As it is not possible to statically intialize codelet in C++,
         we do it via this function */
      template <typename T, typename PoolAlloc>
      void codelet_init() {

         // Initialize init_node StarPU codelet
         starpu_codelet_init(&cl_init_node);
         cl_init_node.where = STARPU_CPU;
         cl_init_node.nbuffers = STARPU_VARIABLE_NBUFFERS;
         cl_init_node.name = "INIT_NODE";
         cl_init_node.cpu_funcs[0] = init_node_cpu_func<T, PoolAlloc>;

         // Initialize factorize_block StarPU codelet
         starpu_codelet_init(&cl_factorize_block);
         cl_factorize_block.where = STARPU_CPU;
         cl_factorize_block.nbuffers = STARPU_VARIABLE_NBUFFERS;
         cl_factorize_block.name = "FACTO_BLK";
         cl_factorize_block.cpu_funcs[0] = factorize_block_cpu_func;

         // Initialize solve_block StarPU codelet
         starpu_codelet_init(&cl_solve_block);
         cl_solve_block.where = STARPU_CPU;
         cl_solve_block.nbuffers = STARPU_VARIABLE_NBUFFERS;
         cl_solve_block.name = "SOLVE_BLK";
         cl_solve_block.cpu_funcs[0] = solve_block_cpu_func;

         // Initialize update_block StarPU codelet
         starpu_codelet_init(&cl_update_block);
         cl_update_block.where = STARPU_CPU;
         cl_update_block.nbuffers = STARPU_VARIABLE_NBUFFERS;
         cl_update_block.name = "UPDATE_BLK";
         cl_update_block.cpu_funcs[0] = update_block_cpu_func;

         // Initialize update_between StarPU codelet
         starpu_codelet_init(&cl_update_between);
         cl_update_between.where = STARPU_CPU;
         cl_update_between.nbuffers = STARPU_VARIABLE_NBUFFERS;
         cl_update_between.name = "UPDATE_BETWEEN_BLK";
         cl_update_between.cpu_funcs[0] = update_between_cpu_func<T, PoolAlloc>;

      }   
}} /* namespaces spldlt::starpu  */