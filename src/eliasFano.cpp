#include <Rcpp.h>
#include <iostream>
#include <bitset>
#include <algorithm>
#include <functional>
#include <iterator>
#include <set>
#include <cmath>
// #include <RcppArmadillo.h>

// [[Rcpp::depends(RcppArmadillo)]]

#include "scfind_types.h"
#include "fp_growth.hpp"


// the bits used for the encoding
#define BITS 32


typedef std::pair<unsigned short, std::bitset<BITS> > BitSet32;
typedef std::vector<bool> BoolVec;
typedef std::string CellType;



// struct that holds the quantization vector
typedef struct{
  double mu;
  double sigma;
  std::vector<bool> quantile;
} Quantile;

typedef struct
{
  BoolVec H;
  BoolVec L;
  int l;
  float idf;
  Quantile expr;
} EliasFano;


struct Cell_ID
{
  unsigned int num;
  const CellType* cell_type;
  // Hashing
  size_t operator()() const
  {
    return std::hash<const CellType*>{}(cell_type) ^ std::hash<unsigned int>{}(num);
  }
  
  size_t operator==(const struct Cell_ID& obj) const 
  {
    return (num == obj.num) && (cell_type == obj.cell_type);
  }
};


typedef struct Cell_ID CellID;

namespace std
{
  template<>
  struct hash<CellID>
  {
    size_t operator()(const CellID& obj) const
    {
      // Return overloaded operator
      return obj();
    }
  };
}



std::string str_join( const std::vector<std::string>& elements, const char* const separator)
{
    switch (elements.size())
    {
        case 0:
            return "";
        case 1:
            return *(elements.begin());
        default:
            std::ostringstream os; 
            std::copy(elements.cbegin(), elements.cend() - 1, std::ostream_iterator<std::string>(os, separator));
            os << *elements.crbegin();
            return os.str();
    }
}





inline std::bitset<BITS> int2bin_core(const unsigned int id)
{
  // we have a fixed BITS bitset so we have to offset 
  // the leading zeros whenever we need to access the elements
  return std::bitset<BITS>(id);
}





inline BitSet32 int2bin_bounded(const unsigned int id, unsigned int min_bit_length)
{
  // Check if number is larger than the desired size in bits
  unsigned short bit_set_length = id == 0 ? 0 : __builtin_clz(id);
  bit_set_length = bit_set_length > min_bit_length ? bit_set_length : min_bit_length;
  return make_pair(bit_set_length, int2bin_core(id));
}


// These functions work with 1-based indexes on arrays, 
// __builtin_clz has undefined behavior with 0
inline BitSet32 int2bin(unsigned int id)
{
  return make_pair(__builtin_clz(id), int2bin_core(id));
}



inline double normalCDF(double x, double mu, double sigma)
{
  // this is an inline function for the cdm of the normal distribution
  // it depends on the cmath library where it contains the erfc function
  // it return a value ranging from zero to one 
  
  return 1 - (0.5 * erfc( (x - mu)/ (sigma * M_SQRT1_2) ));
   
}


// Accepts a vector, transforms and returns a quantization logical vector
// This function aims for space efficiency of the expression vector
Quantile lognormalcdf(std::vector<int> ids, const Rcpp::NumericVector& v, unsigned int bits)
{
  Quantile expr;
  expr.mu = std::accumulate(ids.begin(),ids.end(), 0, [&v](const double& mean, const int& index){
      return  mean + v[index];
    }) / ids.size();
  
  expr.sigma = sqrt(std::accumulate(ids.begin(), ids.end(), 0, [&v, &expr](const double& variance, const int& index){
        return pow(expr.mu - v[index], 2);
      }) / ids.size());
  // initialize vector with zeros
  expr.quantile.resize(ids.size() * bits, 0);
  int expr_quantile_i = 0;
  for (auto& s : v)
  {
    unsigned int t = round(normalCDF(s, expr.mu, expr.sigma) * (1 << bits));  
    std::bitset<BITS> q = int2bin_core(t);
    for (int i = 0; i < bits; ++i)
    {
      expr.quantile[expr_quantile_i++] = q[i];
    }
  }
  return expr;
}




class EliasFanoDB;
RCPP_EXPOSED_CLASS(EliasFanoDB)


class EliasFanoDB
{

 public:
  typedef std::unordered_map<const CellType*, const EliasFano*> EliasFanoIndex;
  // gene -> cell type -> eliasFano
  typedef std::unordered_map<std::string, EliasFanoIndex > CellTypeIndex;
  typedef std::deque<EliasFano> ExpressionMatrix;
  // Store the gene metadata, only presence in the data
  typedef std::map<std::string, unsigned int> GeneIndex;

 private:
  CellTypeIndex metadata;
  ExpressionMatrix ef_data;
  std::set<CellType> cell_types;
  GeneIndex gene_counts;
  unsigned int total_cells;
  
  bool global_indices;
  int warnings;

  void insertToDB(int ef_index, const std::string& gene_name, const std::string& cell_type)
  {
    if(ef_index == -1)
    {
      // Something went wrong so do not do anything
      this->warnings++;
      return;
    }

    EliasFano* ef = &(this->ef_data[ef_index]);
    
    if(metadata.find(gene_name) == metadata.end())
    {
      metadata[gene_name] = EliasFanoIndex();
    }
    
    std::set<CellType>::iterator celltype_record = this->cell_types.find(cell_type);
    if (celltype_record == this->cell_types.end())
    {
      this->cell_types.insert(cell_type);
    }
    celltype_record = this->cell_types.find(cell_type);
    
    const CellType* address = &(*celltype_record);
    metadata[gene_name][address] = ef;
    
 } 

  long eliasFanoCoding(const std::vector<int>& ids, const Rcpp::NumericVector& values) 
  {
    
    if(ids.empty())
    {
      return -1;
    }
    int items = values.size();

    
    EliasFano ef;
    ef.l = int(log2(items / (float)ids.size()) + 0.5) + 1;
    ef.idf = log2(items / (float)ids.size());
    int l = ef.l;

    int prev_indexH = 0;
    ef.L.resize(l * ids.size(), false);

    BoolVec::iterator l_iter = ef.L.begin();
    ef.expr = lognormalcdf(ids, values, 2);
    
    
    
    for (auto expr = ids.begin(); expr != ids.end(); ++expr)
    {
      BitSet32 c = int2bin_bounded(*expr, l);
    
      for( int i = 0; i < l; i++, ++l_iter)
      {
        // TODO(Nikos) optimize this ? check if c.second[i] is false and THEN assign
        *l_iter = c.second[i];
      }
        //Use a unary code for the high bits
        unsigned int upper_bits = (*expr >> l);
        unsigned int m =  ef.H.size() + upper_bits - prev_indexH + 1;
        prev_indexH = upper_bits;
        ef.H.resize(m, false);
        ef.H[m - 1] = true;
    }
    ef_data.push_back(ef);
    // return the index of the ef_data in the deque
    return ef_data.size() - 1;
  }

  std::vector<int> eliasFanoDecoding(const EliasFano& ef)
  {
    
    std::vector<int> ids(ef.L.size() / ef.l);

    // This step inflates the vector by a factor of 8
    std::vector<char> H;
    H.reserve(ef.H.size());
    H.insert(H.end(), ef.H.begin(), ef.H.end());
    

    unsigned int H_i = 0;
    // Warning: Very very dodgy I might want to replace this with a check in the loop
    auto prev_it = H.begin() - 1;
    int i = 0;
    for (auto true_it = std::find(H.begin(), H.end(), true); 
         true_it != H.end() && i < ids.size(); 
         true_it = std::find(true_it + 1, H.end(), true), ++i)
    {
      size_t offset  = std::distance(prev_it, true_it);
      prev_it = true_it;
      H_i += offset - 1;
      int id = H_i << ef.l;
      for (unsigned short k = 0; k < ef.l; ++k)
      {
        id |= (ef.L[(i * ef.l) + k] << k); 
      }
      ids[i] = id;
    }
    return ids;
  }



 public:

  // constructor
  EliasFanoDB(): global_indices(false), warnings(0), total_cells(0)
  {
    
  }
  // This is invoked on slices of the expression matrix of the dataset 
  long encodeMatrix(const std::string& cell_type, const Rcpp::NumericMatrix& gene_matrix)
  {
    int items = gene_matrix.ncol();
    Rcpp::CharacterVector genes = Rcpp::rownames(gene_matrix);
    std::vector<std::string> gene_names;
    gene_names.reserve(genes.size());


    // Increase the cell number present in the index
    this->total_cells += gene_matrix.ncol();

    for(Rcpp::CharacterVector::iterator gene_it = genes.begin(); gene_it != genes.end(); ++gene_it)
    {
      std::string gene_name = Rcpp::as<std::string>(*gene_it);
      gene_names.push_back(gene_name);
      // If this is the first time this gene occurs initialize a counter
      if (this->gene_counts.find(gene_name) == this->gene_counts.end())
      {
        this->gene_counts[gene_name] = 0;
      }
    }
     
    for(unsigned int gene_row = 0; gene_row < gene_matrix.nrow(); ++gene_row)
    {
      const Rcpp::NumericVector& expression_vector = gene_matrix(gene_row, Rcpp::_);
      std::deque<int> sparse_index;
      int i = 0;
      for (Rcpp::NumericVector::const_iterator expr = expression_vector.begin(); expr != expression_vector.end(); ++expr)
      {
        i++; 
        if (*expr > 0)
        {
          sparse_index.push_back(i);
        }
      }
      if ( i == 0)
      {
        warnings++;
        continue;
      }
      std::vector<int> ids(sparse_index.begin(), sparse_index.end());
      this->gene_counts[gene_names[gene_row]] += ids.size();
      insertToDB(eliasFanoCoding(ids, expression_vector), gene_names[gene_row], cell_type);
    }
    return 0;
    //std::cerr << "Total Warnings: "<<warnings << std::endl;
  }

  Rcpp::List total_genes()
  {
    Rcpp::List t;
    for(auto & d : metadata)
    {
      t.push_back(Rcpp::wrap(d.first));
    }
    return t;
  }

  Rcpp::List queryGenes(const Rcpp::CharacterVector& gene_names)
  {
    Rcpp::List t;
    for (Rcpp::CharacterVector::const_iterator it = gene_names.begin(); it != gene_names.end(); ++it)
    {
      
      
      std::string gene_name = Rcpp::as<std::string>(*it);
      
      //t.add(Rcpp::wrap(gene_names[i]), Rcpp::List::create());
      Rcpp::List cell_types;
      
      if (metadata.find(gene_name) == metadata.end())
      {
        
        std::cout << "Gene " << gene_name << " not found in the index " << std::endl;
        continue;
      }

      auto gene_meta = metadata[gene_name];
      for (auto const& dat : gene_meta)
      {

        std::vector<int> ids = eliasFanoDecoding(*(dat.second));
        cell_types[*(dat.first)] = Rcpp::wrap(ids);
      }
      t[gene_name] = cell_types;
    }

    return t;
  }
  
  size_t dataMemoryFootprint()
  {
    size_t bytes = 0;
    for(auto & d : ef_data)
    {
      bytes += int((d.H.size() / 8) + 1);
      bytes += int((d.L.size() / 8) + 1);
      bytes += int((d.quantile.size() / 8) + 1);
      
    }
    bytes += ef_data.size() * 32; // overhead of l idf and deque struct
    return bytes;
  }

  size_t dbMemoryFootprint()
  {
    size_t bytes = dataMemoryFootprint();

    std::cout << "Raw elias Fano Index size " << bytes / (1024 * 1024) << "MB" << std::endl;
    
    for(auto& d : metadata)
    {
      bytes += d.first.size();
      bytes += d.second.size() * 12;
    }
    return bytes;
  }


  // And query
  Rcpp::List findCellTypes(const Rcpp::CharacterVector& gene_names)
  {
    
    std::unordered_map<const CellType*, std::set<std::string> > cell_types;
    std::vector<std::string> genes;
    for (Rcpp::CharacterVector::const_iterator it = gene_names.begin(); it != gene_names.end(); ++it)
    {
      std::string gene_name = Rcpp::as<std::string>(*it);
      bool empty_set = false;
      // check if gene exists in the database
      auto db_it = metadata.find(gene_name);
      if ( db_it == metadata.end())
      {
        std::cout << gene_name << " is ignored, not found in the index"<< std::endl;
        continue;
      }

      // iterate cell type
      for (auto const& ct_it : db_it->second)
      {
        if (cell_types.find(ct_it.first) == cell_types.end())
        {
          cell_types[ct_it.first] = std::set<std::string>();
        }
        cell_types[ct_it.first].insert(gene_name);
      }
      genes.push_back(gene_name);
      
    }
    Rcpp::List t;
    
    for (auto const& ct : cell_types)
    {
      // TODO(fix) empty case?
      bool empty_set = false;
      if (ct.second.size() != genes.size())
      {
        continue;
      }
      std::vector<int> ef = eliasFanoDecoding(*(metadata[*(ct.second.begin())][ct.first]));
      std::set<int> int_cells(ef.begin(), ef.end());
      for (auto const& g : ct.second)
      {
        auto cells = eliasFanoDecoding(*(metadata[g][ct.first]));
        std::set<int> new_set;
        std::set_intersection(int_cells.begin(), int_cells.end(), cells.begin(), cells.end(), std::inserter(new_set, new_set.begin()));
        if(new_set.size() != 0)
        {
          int_cells = new_set;
        }
        else
        {
          empty_set = true;
          break;
        }
      } 
      if (!empty_set)
      {
        // std::vector<int> res(int_cells.begin(), int_c;
        t[*(ct.first)] = Rcpp::wrap(int_cells);
      }
    }
    return t;
  }

  Rcpp::List findMarkerGenes(const Rcpp::CharacterVector& gene_list, unsigned int min_support_cutoff = 5)
  {
    Rcpp::List t;
    std::unordered_map<CellID, Transaction > cell_index;
    Rcpp::List genes_results = queryGenes(gene_list);
    const Rcpp::CharacterVector gene_names = genes_results.names();
    for (auto const& gene_hit : gene_names)
    {
      auto gene_name = Rcpp::as<std::string>(gene_hit);
      const Rcpp::List& cell_types_hits = genes_results[gene_name];
      const Rcpp::CharacterVector& cell_type_names = cell_types_hits.names();
      for (auto const& _ct : cell_type_names)
      {
        std::string ct = Rcpp::as<std::string>(_ct);
        
        std::vector<unsigned int> ids  = Rcpp::as<std::vector<unsigned int> >(cell_types_hits[ct]);
        const CellType* ct_p = &(*(this->cell_types.find(ct)));
        
        for (auto const& id : ids)
        {
          CellID unique_id = {id, ct_p};
          // Initialize the data structure
          if(cell_index.find(unique_id) == cell_index.end())
          {
            cell_index[unique_id] = Transaction();
          }
          cell_index[unique_id].push_back(gene_name);
          
        }
      }
    }

    std::cout << "Query Done: found " << cell_index.size() << " rules" << std::endl;

    // Run FPGrowth
    std::vector<Transaction> transactions;
    transactions.reserve(cell_index.size());
    for (auto const& cell : cell_index)
    {
      // Maybe sort?
      transactions.push_back(cell.second);
    }
    
    // cutoff should be user defined
    const FPTree fptree{transactions, min_support_cutoff};
    const std::set<Pattern> patterns = fptree_growth(fptree);
    
    
    std::vector<std::pair<std::string, double> > tfidf;
    for ( auto const& item : patterns)
    {
      Rcpp::List gene_query;
      const auto& gene_set = item.first;
      double query_score = log(this->total_cells) * gene_set.size();
      std::vector<const CellType*> gene_set_cell_types;
      for (auto const& gene: gene_set)
      {
        
        query_score -= log(this->gene_counts[gene]);
        std::vector<const CellType*> gene_cell_types;
        for(auto const& ct : metadata[gene])
        {
          gene_cell_types.push_back(ct.first);
          std::cout << *(ct.first) << " on "<<  ct.first << std::endl;
        }
        if (gene_set_cell_types.empty())
        {
          gene_set_cell_types = gene_cell_types;
        }
        else
        {
          std::vector<const CellType*> intersected;
          
          std::set_intersection(
            gene_set_cell_types.begin(), 
            gene_set_cell_types.end(), 
            gene_cell_types.begin(), 
            gene_cell_types.end(), 
            std::back_inserter(intersected));
          
          gene_set_cell_types = intersected;
        }
        
      }
      std::string view_string = str_join(std::vector<Item>(gene_set.begin(), gene_set.end()), ",");
      query_score *= log(item.second);
      double ct_idf = 0;
      for(auto const& gene: gene_set)
      {
        for (auto const& ct : gene_set_cell_types)
        {
          ct_idf += metadata[gene][ct]->idf;
        }
      }
      query_score /= ct_idf;
      std::cout << 
        view_string << " score: " << query_score 
        << " support: " << item.second <<" "<< gene_set_cell_types.size() << std:: endl;
      tfidf.push_back(make_pair(view_string, query_score));
      
      // t.push_back(Rcpp::List::create(Rcpp::_["query"] = Rcpp::wrap(view_string), Rcpp::_["score"] = Rcpp::wrap(query_score)));
    }

    return t;
  }


  int dbSize()
  {
    std::cout << metadata.size() << "genes in the DB" << std::endl;
    return ef_data.size();
    
  }

  int sample(int index)
  {
    auto iter = metadata.begin();
    for(int i = 0; i < index; i++, ++iter);

    std::cout << "Gene: " << iter->first << std::endl;
    for(auto const& ct : iter->second)
    {
      std::cout << "Cell Type:" << ct.first << std::endl;
      auto v = eliasFanoDecoding(*(ct.second));
      for( auto& cell : v)
      {
        std::cout << cell << ", ";
      }
      std::cout << std::endl;
      
    }
    return 0;
    
  }

  std::vector<int> decode(int index)
  {
    if(index >= dbSize())
    {
      std::cerr << "Invalid index for database with size "<< dbSize() << std::endl;
      return std::vector<int>();
    }
    return eliasFanoDecoding(ef_data[index]);
  }

  int mergeDB(const EliasFanoDB& db)
  {    
    EliasFanoDB extdb(db);
    
    // the DB will grow by this amount of cells
    this->total_cells += extdb.total_cells;
    
    // Copy data on the external object
    // and update pointers
    // In order to maintain consistency
    for ( auto& gene: extdb.metadata)
    {
      for( auto& ct : gene.second)
      {
        ef_data.push_back(*ct.second);
        
        // Update with the new entry
        ct.second = &ef_data.back();
      }
    }

    for(auto const& gene : extdb.metadata)
    {
      // Update cell counts for the individual gene
      if (this->gene_counts.find(gene.first) == this->gene_counts.end())
      {
        this->gene_counts[gene.first] = 0;
      }
      this->gene_counts[gene.first] += extdb.gene_counts[gene.first];
      

      // if gene does not exist yet initialize entry in metadata
      if(metadata.find(gene.first) == metadata.end())
      {
        metadata[gene.first] = EliasFanoIndex();
      }

      // Insert new cell types
      for(auto const& ct: extdb.metadata[gene.first])
      {
        cell_types.insert(*(ct.first));
        auto new_ct = cell_types.find(*(ct.first));
        // set the reference at the map
        metadata[gene.first][&(*new_ct)] = ct.second;
      }
    }
    return 0;
  }

};

RCPP_MODULE(EliasFanoDB)
{
  Rcpp::class_<EliasFanoDB>("EliasFanoDB")
    .constructor()
    .method("indexMatrix", &EliasFanoDB::encodeMatrix)
    .method("queryGenes", &EliasFanoDB::queryGenes)
    .method("dbSize", &EliasFanoDB::dbSize)
    .method("decode", &EliasFanoDB::decode)
    .method("mergeDB", &EliasFanoDB::mergeDB)
    .method("sample", &EliasFanoDB::sample)
    .method("genes", &EliasFanoDB::total_genes)
    .method("findCellTypes", &EliasFanoDB::findCellTypes)
    .method("efMemoryFootprint", &EliasFanoDB::dataMemoryFootprint)
    .method("dbMemoryFootprint", &EliasFanoDB::dbMemoryFootprint)
    .method("findMarkerGenes", &EliasFanoDB::findMarkerGenes);

}




