#pragma once

// SyVLER
#include "NumericFront.hxx"
#include "tasks/tasks_unsym.hxx"

// SSIDS
#include "ssids/cpu/cpu_iface.hxx"
#include "ssids/cpu/Workspace.hxx"

namespace spldlt {

   template <typename T, typename PoolAlloc>
   void factor_front_unsym_app(
         struct cpu_factor_options& options,
         NumericFront<T, PoolAlloc> &node
         ) {

      // printf("[factor_front_unsym_app]\n");

      // typedef typename std::allocator_traits<PoolAlloc>::template rebind_alloc<int> IntAlloc;
      typedef typename NumericFront<T, PoolAlloc>::IntAlloc IntAlloc;

      int m = node.get_nrow(); // Frontal matrix order
      int n = node.get_ncol(); // Number of fully-summed rows/columns 
      int nr = node.get_nr(); // number of block rows
      int nc = node.get_nc(); // number of fully-summed block columns
      size_t contrib_dimn = m-n;
      int blksz = node.blksz;
      ColumnData<T, IntAlloc>& cdata = *node.cdata;

      T u = options.u; // Threshold parameter

      for(int k = 0; k < nc; ++k) {
         
         BlockUnsym<T>& dblk = node.get_block_unsym(k, k);
         int *rperm = &node.perm [k*blksz];
         int *cperm = &node.cperm[k*blksz];
         factor_block_unsym_app_task(dblk, rperm, cperm, cdata);

         // Compute L factor
         for (int i = 0; i < k; ++i) {
            // Super-diagonal block
            BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
            appyU_block_app_task(dblk, u, lblk, cdata);
         }
         for (int i = k+1; i < nr; ++i) {
            // Sub-diagonal block
            BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
            appyU_block_app_task(dblk, u, lblk, cdata);
         }

         // Restore failed entries
         for (int i = 0; i < nr; ++i) {
            BlockUnsym<T>& blk = node.get_block_unsym(i, k);
            restore_block_unsym_app_task(k, blk, cdata);
         }

         // Compute U factor
         for (int j = 0; j < k; ++j) {
            // Left-diagonal block
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);
            appyL_block_app_task(dblk, ublk, cdata);
         }
         for (int j = k+1; j < nr; ++j) {
            // Right-diagonal block
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);
            appyL_block_app_task(dblk, ublk, cdata);
         }
         
         // Update previously failed entries
         for (int j = 0; j < k; ++j) {
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);
            for (int i = 0; i < k; ++i) {
               BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
               BlockUnsym<T>& blk = node.get_block_unsym(i, j);
               update_block_unsym_app_task(lblk, ublk, blk, cdata);
            }
         }
         
         // Update uneliminated entries in L
         for (int j = 0; j < k; ++j) {
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);
            for (int i = k+1; i < nr; ++i) {
               BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
               BlockUnsym<T>& blk = node.get_block_unsym(i, j);
               update_block_unsym_app_task(lblk, ublk, blk, cdata);
            }
         }

         // Update uneliminated entries in U
         for (int j = k+1; j < nr; ++j) {
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);
            for (int i = 0; i < k; ++i) {
               BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
               BlockUnsym<T>& blk = node.get_block_unsym(i, j);
               update_block_unsym_app_task(lblk, ublk, blk, cdata);
            }       
         }
      
         // Udpdate trailing submatrix
         int en = (n-1)/blksz; // Last block-row/column in factors
         for (int j = k+1; j < nr; ++j) {
            
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);

            for (int i =  k+1; i < nr; ++i) {
               // Loop if we are in the cb
               if ((i > en) && (j > en)) continue;

               BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
               BlockUnsym<T>& blk = node.get_block_unsym(i, j);
               update_block_unsym_app_task(lblk, ublk, blk, cdata);
            }
         }

         // Update contribution blocks
         if (contrib_dimn>0) {
            // Update contribution block
            int rsa = n/blksz; // Last block-row/column in factors
         
            for (int j = rsa; j < nr; ++j) {

               BlockUnsym<T>& ublk = node.get_block_unsym(k, j);

               for (int i = rsa; i < nr; ++i) {

                  BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
                  Tile<T, PoolAlloc>& cblk = node.get_contrib_block(i, j);
                  
                  update_cb_block_unsym_app_task(lblk, ublk, cblk, cdata);               
               }
            }
         }

      }

   }
   
   /// @brief Task-based front factorization routine using Restricted
   /// Pivoting (RP)
   /// @Note No delays, potentially unstable
   template <typename T, typename PoolAlloc>
   void factor_front_unsym_rp(
         NumericFront<T, PoolAlloc> &node,
         std::vector<spral::ssids::cpu::Workspace>& workspaces) {

      // Extract front info
      int m = node.get_nrow(); // Frontal matrix order
      int n = node.get_ncol(); // Number of fully-summed rows/columns 
      int nr = node.get_nr(); // number of block rows
      int nc = node.get_nc(); // number of block columns
      size_t contrib_dimn = m-n;
      int blksz = node.blksz;
      
      printf("[factor_front_unsym_rp] m = %d\n", m);
      printf("[factor_front_unsym_rp] n = %d\n", n);
      
      for(int k = 0; k < nc; ++k) {
         BlockUnsym<T>& dblk = node.get_block_unsym(k, k);
         int *perm = &node.perm[k*blksz];
         
         factor_block_lu_pp_task(dblk, perm);

         // Apply permutation
         for (int j = 0; j < k; ++j) {
            BlockUnsym<T>& rblk = node.get_block_unsym(k, j);
            // Apply row permutation on left-diagonal blocks 
            apply_rperm_block_task(dblk, rblk, workspaces);
         }

         // Apply permutation and compute U factors
         for (int j = k+1; j < nc; ++j) {
            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);

            applyL_block_task(dblk, ublk, workspaces);
         }

         // Compute L factors
         for (int i =  k+1; i < nr; ++i) {
            BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
            
            applyU_block_task(dblk, lblk);            
         }

         int en = (n-1)/blksz; // Last block-row/column in factors
            
         // Udpdate trailing submatrix
         for (int j = k+1; j < nr; ++j) {

            BlockUnsym<T>& ublk = node.get_block_unsym(k, j);

            for (int i =  k+1; i < nr; ++i) {
               // Loop if we are in the cb
               if ((i > en) && (j > en)) continue;

               BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
               BlockUnsym<T>& blk = node.get_block_unsym(i, j);
               
               update_block_lu_task(lblk, ublk, blk);
            }
         }
         
         if (contrib_dimn>0) {
            // Update contribution block
            int rsa = n/blksz; // Last block-row/column in factors
         
            for (int j = rsa; j < nr; ++j) {

               BlockUnsym<T>& ublk = node.get_block_unsym(k, j);

               for (int i = rsa; i < nr; ++i) {

                  BlockUnsym<T>& lblk = node.get_block_unsym(i, k);
                  Tile<T, PoolAlloc>& cblk = node.get_contrib_block(i, j);
                  
                  update_cb_block_lu_task(lblk, ublk, cblk);
               
               }
            }
         }
      }

      // Note: we do not check for stability       
      node.nelim = n; // We eliminated all fully-summed rows/columns
      node.ndelay_out = 0; // No delays

   }

}
