
#' The scfind main class object
#' @export
setClass("SCFind", representation(index = "hash", datasets = "character"))

#' @examples TODO
#' 
#' @export
setGeneric(name = "buildCellTypeIndex",
           def = function(sce,
                          dataset.name = '',
                          assay.name = 'logcounts',
                          cell.type.label = 'cell_type1')
           {
               standardGeneric("buildCellTypeIndex")
           })


#' 
#' @examples TODO
#' @export 
setGeneric(name = "mergeDataset", def = function(object, new.object) {
    standardGeneric("mergeDataset")
})
 
#' @examples TODO
#' @export 
setGeneric(name = "mergeSCE", def = function(object, sce, dataset.name) {
    standardGeneric("mergeSCE")
})


#' @export
#' 
#' @examples TODO
#' 
setGeneric(name = "queryGene", def = function(object, gene) {
    standardGeneric("queryGene")
})

#' @export
#' 
#' @examples
#' library(SingleCellExperiment)
#' sce <- SingleCellExperiment(assays = list(normcounts = as.matrix(yan)), colData = ann)
#' # this is needed to calculate dropout rate for feature selection
#' # important: normcounts have the same zeros as raw counts (fpkm)
#' counts(sce) <- normcounts(sce)
#' logcounts(sce) <- log2(normcounts(sce) + 1)
#' # use gene names as feature symbols
#' rowData(sce)$feature_symbol <- rownames(sce)
#' isSpike(sce, 'ERCC') <- grepl('^ERCC-', rownames(sce))
#' # remove features with duplicated names
#' sce <- sce[!duplicated(rownames(sce)), ]
#' index <- buildCellIndex(sce)
#' res <- findCell(index, genelist = c('SOX6', 'SNAI3'))
#' 
setGeneric(name = "findCellTypes", function(object, gene.list) {
    standardGeneric("findCellTypes")
})